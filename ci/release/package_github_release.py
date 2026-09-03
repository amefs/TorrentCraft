#!/usr/bin/env python3
"""Package GitHub release materials while keeping only binaries flat."""

from __future__ import annotations

import argparse
import gzip
import hashlib
import os
import shutil
import subprocess
import tarfile
import tempfile
import time
import zipfile
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class Platform:
    name: str
    extension: str
    archive_suffix: str


PLATFORMS = (
    Platform("linux-musl-x86_64", "", ".tar.gz"),
    Platform("windows-x86_64", ".exe", ".zip"),
)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def one_file(root: Path, pattern: str, description: str) -> Path:
    matches = sorted(path for path in root.rglob(pattern) if path.is_file())
    if len(matches) != 1:
        names = ", ".join(str(path) for path in matches) or "none"
        raise RuntimeError(
            f"expected exactly one {description} matching {pattern!r}; found {names}"
        )
    return matches[0]


def copy_into(source: Path, destination: Path, consumed: set[Path]) -> None:
    if destination.exists():
        raise RuntimeError(f"duplicate package destination: {destination}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)
    consumed.add(source)


def normalized_tar_info(info: tarfile.TarInfo, epoch: int) -> tarfile.TarInfo:
    info.mtime = epoch
    info.uid = 0
    info.gid = 0
    info.uname = "root"
    info.gname = "root"
    return info


def write_tar(root: Path, destination: Path, epoch: int) -> None:
    with destination.open("wb") as raw:
        with gzip.GzipFile(fileobj=raw, mode="wb", mtime=epoch) as compressed:
            with tarfile.open(fileobj=compressed, mode="w") as archive:
                for path in sorted(root.rglob("*")):
                    if not path.is_file() and not path.is_symlink():
                        continue
                    relative = path.relative_to(root).as_posix()
                    info = normalized_tar_info(
                        archive.gettarinfo(str(path), arcname=relative), epoch
                    )
                    if path.is_file() and not path.is_symlink():
                        with path.open("rb") as stream:
                            archive.addfile(info, stream)
                    else:
                        archive.addfile(info)


def write_zip(root: Path, destination: Path, epoch: int) -> None:
    timestamp = time.gmtime(epoch)
    date_time = (
        max(1980, timestamp.tm_year),
        timestamp.tm_mon,
        timestamp.tm_mday,
        timestamp.tm_hour,
        timestamp.tm_min,
        timestamp.tm_sec // 2 * 2,
    )
    with zipfile.ZipFile(destination, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(root.rglob("*")):
            if not path.is_file() and not path.is_symlink():
                continue
            relative = path.relative_to(root).as_posix()
            info = zipfile.ZipInfo(relative, date_time)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            archive.writestr(info, path.read_bytes())


def archive_epoch() -> int:
    configured = os.environ.get("SOURCE_DATE_EPOCH")
    if configured:
        return int(configured)
    return int(
        subprocess.check_output(
            ["git", "show", "-s", "--format=%ct", "HEAD"], text=True
        ).strip()
    )


def package_platform(
    artifact_root: Path,
    output_dir: Path,
    version: str,
    platform: Platform,
    epoch: int,
) -> list[Path]:
    stem = f"TorrentCraft-{version}-{platform.name}"
    binary_names = (
        f"{stem}-cli{platform.extension}",
        f"{stem}-gui{platform.extension}",
    )
    consumed: set[Path] = set()
    binaries: list[Path] = []

    for name in binary_names:
        source = one_file(artifact_root, name, f"{name} executable")
        destination = output_dir / name
        copy_into(source, destination, consumed)
        binaries.append(destination)

    with tempfile.TemporaryDirectory(prefix=f"torrentcraft-{platform.name}-") as temporary:
        package_root = Path(temporary)
        metadata_dir = package_root / "metadata"
        for suffix in ("SBOM.spdx.json", "SECURITY-AUDIT.json", "PROVENANCE.json"):
            source = one_file(artifact_root, f"{stem}.{suffix}", f"{stem}.{suffix}")
            copy_into(source, metadata_dir / source.name, consumed)

        compliance = one_file(
            artifact_root,
            f"{stem}-Qt-LGPL-compliance.*",
            f"{stem} Qt LGPL compliance archive",
        )
        copy_into(compliance, package_root / "compliance" / compliance.name, consumed)

        test_reports = sorted(
            path for path in artifact_root.rglob("test-*.xml") if path.is_file()
        )
        if not test_reports:
            raise RuntimeError(f"missing test report for {platform.name}")
        for report in test_reports:
            copy_into(report, package_root / "tests" / report.name, consumed)
        summary = one_file(
            artifact_root,
            "phase8-test-summary.json",
            f"{platform.name} test summary",
        )
        copy_into(summary, package_root / "tests" / summary.name, consumed)

        diagnostics = sorted(
            path
            for path in artifact_root.rglob("*")
            if path.is_file() and "vcpkg-diagnostics" in path.parts
        )
        for diagnostic in diagnostics:
            marker = diagnostic.parts.index("vcpkg-diagnostics")
            relative = Path(*diagnostic.parts[marker + 1 :])
            copy_into(
                diagnostic,
                package_root / "tests" / "vcpkg-diagnostics" / relative,
                consumed,
            )

        source_names = (
            f"TorrentCraft-{version}-source.tar.gz",
            f"TorrentCraft-{version}-source.PROVENANCE.json",
            f"TorrentCraft-{version}-source.SBOM.spdx.json",
            f"TorrentCraft-{version}-source.SECURITY-AUDIT.json",
        )
        for name in source_names:
            source = one_file(artifact_root, name, name)
            copy_into(source, package_root / "source" / source.name, consumed)

        all_files = {path for path in artifact_root.rglob("*") if path.is_file()}
        ignored = {path for path in all_files if path.name == "SHA256SUMS"}
        unexpected = sorted(all_files - consumed - ignored)
        if unexpected:
            names = ", ".join(str(path) for path in unexpected)
            raise RuntimeError(f"unclassified release files for {platform.name}: {names}")

        manifest_lines = [f"{sha256(binary)}  {binary.name}" for binary in binaries]
        for path in sorted(package_root.rglob("*")):
            if path.is_file() and path.name != "SHA256SUMS":
                manifest_lines.append(
                    f"{sha256(path)}  {path.relative_to(package_root).as_posix()}"
                )
        (package_root / "SHA256SUMS").write_text(
            "\n".join(manifest_lines) + "\n", encoding="utf-8"
        )

        package_name = f"{stem}-support{platform.archive_suffix}"
        package_path = output_dir / package_name
        if platform.archive_suffix == ".zip":
            write_zip(package_root, package_path, epoch)
        else:
            write_tar(package_root, package_path, epoch)

    return [*binaries, package_path]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--linux-root", type=Path, required=True)
    parser.add_argument("--windows-root", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--epoch", type=int)
    args = parser.parse_args()

    version = args.version.removeprefix("v")
    args.output_dir.mkdir(parents=True, exist_ok=True)
    if any(args.output_dir.iterdir()):
        raise RuntimeError(f"release asset directory must be empty: {args.output_dir}")

    roots = {
        "linux-musl-x86_64": args.linux_root.resolve(),
        "windows-x86_64": args.windows_root.resolve(),
    }
    assets: list[Path] = []
    for platform in PLATFORMS:
        root = roots[platform.name]
        if not root.is_dir():
            raise RuntimeError(f"missing {platform.name} artifact directory: {root}")
        assets.extend(
            package_platform(
                root,
                args.output_dir,
                version,
                platform,
                args.epoch if args.epoch is not None else archive_epoch(),
            )
        )
    for asset in assets:
        print(asset)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
