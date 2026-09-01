#!/usr/bin/env python3
"""Build deterministic static release artifacts."""

from __future__ import annotations

import argparse
import datetime as dt
import gzip
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile
import time
import urllib.request
import zipfile
from pathlib import Path
from typing import Iterable


def run(command: list[str], cwd: Path) -> None:
    subprocess.run(command, cwd=cwd, check=True)


def source_commit(source: Path) -> str:
    value = os.environ.get("CI_COMMIT_SHA")
    if value:
        return value
    return subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=source, text=True
    ).strip()


def source_epoch(source: Path) -> int:
    value = os.environ.get("SOURCE_DATE_EPOCH")
    if value:
        return int(value)
    timestamp = os.environ.get("CI_COMMIT_TIMESTAMP")
    if timestamp:
        parsed = dt.datetime.fromisoformat(timestamp.replace("Z", "+00:00"))
        return int(parsed.timestamp())
    return int(
        subprocess.check_output(
            ["git", "show", "-s", "--format=%ct", "HEAD"], cwd=source, text=True
        ).strip()
    )


def files_under(root: Path) -> Iterable[Path]:
    for path in sorted(root.rglob("*")):
        if path.is_file() or path.is_symlink():
            yield path


def normalize_tar(info: tarfile.TarInfo, epoch: int) -> tarfile.TarInfo:
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
                for path in files_under(root):
                    relative = path.relative_to(root.parent)
                    info = normalize_tar(
                        archive.gettarinfo(str(path), arcname=str(relative)), epoch
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
        for path in files_under(root):
            relative = path.relative_to(root.parent).as_posix()
            info = zipfile.ZipInfo(relative, date_time)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            archive.writestr(info, path.read_bytes())


def write_source_archive(source: Path, destination: Path, version: str, epoch: int) -> None:
    raw_tar = subprocess.check_output(
        [
            "git",
            "archive",
            "--format=tar",
            f"--prefix=TorrentCraft-{version}/",
            "HEAD",
        ],
        cwd=source,
    )
    with destination.open("wb") as raw:
        with gzip.GzipFile(fileobj=raw, mode="wb", mtime=epoch) as compressed:
            compressed.write(raw_tar)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def sha512(path: Path) -> str:
    digest = hashlib.sha512()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def require_file(path: Path, description: str) -> Path:
    if not path.is_file():
        raise RuntimeError(f"missing {description}: {path}")
    return path


def installed_binary(install: Path, name: str) -> Path:
    for candidate in (install / "bin" / name, install / "bin" / f"{name}.exe"):
        if candidate.is_file():
            return candidate
    raise RuntimeError(f"missing installed executable: {name}")


def copy_binary(source: Path, destination: Path) -> None:
    shutil.copy2(source, destination)
    if os.name != "nt":
        destination.chmod(0o755)


def fetch_qt_source(module: dict[str, object], vcpkg_root: Path, cache: Path) -> Path:
    filename = str(module["filename"])
    expected = str(module["sha512"])
    candidates = [vcpkg_root / "downloads" / filename, cache / filename]
    for candidate in candidates:
        if candidate.is_file() and sha512(candidate) == expected:
            return candidate
    destination = cache / filename
    cache.mkdir(parents=True, exist_ok=True)
    errors: list[str] = []
    for url in module["urls"]:
        try:
            with urllib.request.urlopen(str(url), timeout=120) as response:
                with destination.open("wb") as output:
                    shutil.copyfileobj(response, output)
            if sha512(destination) != expected:
                raise RuntimeError("SHA-512 mismatch")
            return destination
        except Exception as error:  # noqa: BLE001 - report all mirror failures together
            errors.append(f"{url}: {error}")
            destination.unlink(missing_ok=True)
    raise RuntimeError(f"could not fetch {filename}: {'; '.join(errors)}")


def extract_qt_license_files(archive_path: Path, destination: Path) -> None:
    wanted = {
        "GPL-2.0-only.txt",
        "GPL-3.0-only.txt",
        "LGPL-3.0-only.txt",
        "LicenseRef-Qt-Commercial.txt",
    }
    with tarfile.open(archive_path, "r:xz") as archive:
        for member in archive.getmembers():
            path = Path(member.name)
            if path.name not in wanted or "LICENSES" not in path.parts or not member.isfile():
                continue
            extracted = archive.extractfile(member)
            if extracted is None:
                continue
            destination.mkdir(parents=True, exist_ok=True)
            (destination / path.name).write_bytes(extracted.read())


def collect_link_material(build: Path, destination: Path) -> dict[str, dict[str, str]]:
    excluded = {"vcpkg_installed", "tests", "Testing", "_deps"}
    suffixes = {".o", ".obj", ".a", ".lib"}
    manifest: dict[str, dict[str, str]] = {}
    material = destination / "files"
    material.mkdir(parents=True, exist_ok=True)
    for path in sorted(build.rglob("*")):
        if not path.is_file() or path.suffix.lower() not in suffixes:
            continue
        relative = path.relative_to(build)
        if any(part in excluded for part in relative.parts):
            continue
        digest = sha256(path)
        target = material / f"{digest}{path.suffix.lower()}"
        if not target.exists():
            shutil.copy2(path, target)
        manifest[relative.as_posix()] = {
            "archive_path": target.relative_to(destination).as_posix(),
            "sha256": digest,
        }
    if not any(name.endswith((".o", ".obj")) for name in manifest):
        raise RuntimeError("no application object files found for the LGPL relinking bundle")
    return manifest


def copy_build_metadata(build: Path, destination: Path) -> None:
    names = {
        "CMakeCache.txt",
        "build.ninja",
        "cmake_install.cmake",
        "compile_commands.json",
        "vcpkg-manifest-install.log",
    }
    for path in sorted(build.rglob("*")):
        if not path.is_file():
            continue
        if path.name not in names and path.suffix.lower() not in {".sln", ".vcxproj"}:
            continue
        relative = path.relative_to(build)
        if "vcpkg_installed" in relative.parts:
            continue
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, target)


def compliance_archive(
    source: Path,
    build: Path,
    installed: Path,
    output: Path,
    vcpkg_root: Path,
    version: str,
    platform: str,
    triplet: str,
    epoch: int,
) -> Path:
    manifest = json.loads(
        (source / "ci" / "release" / "qt-lgpl-sources.json").read_text(encoding="utf-8")
    )
    with tempfile.TemporaryDirectory(prefix="torrentcraft-lgpl-") as temporary:
        temporary_root = Path(temporary)
        root = temporary_root / f"TorrentCraft-{version}-{platform}-Qt-LGPL-compliance"
        qt_sources = root / "qt-source"
        qt_sources.mkdir(parents=True)
        cache = temporary_root / "downloads"
        for module in manifest["modules"]:
            archive = fetch_qt_source(module, vcpkg_root, cache)
            shutil.copy2(archive, qt_sources / archive.name)
            extract_qt_license_files(archive, root / "licenses" / "qt")

        shutil.copy2(source / "LICENSE", root / "licenses" / "TorrentCraft-MIT.txt")
        for package in ("qtbase", "qtsvg"):
            copyright_file = installed / "share" / package / "copyright"
            require_file(copyright_file, f"{package} copyright metadata")
            target = root / "licenses" / "vcpkg" / f"{package}-copyright.txt"
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(copyright_file, target)
            port = vcpkg_root / "ports" / package
            if not port.is_dir():
                raise RuntimeError(f"missing vcpkg port snapshot: {port}")
            shutil.copytree(port, root / "vcpkg-ports" / package)

        object_manifest = collect_link_material(build, root / "application-link-material")
        copy_build_metadata(build, root / "build-metadata")
        (root / "application-link-material" / "SHA256SUMS.json").write_text(
            json.dumps(object_manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        (root / "RELINKING.md").write_text(
            "# Qt LGPL relinking material\n\n"
            f"This bundle corresponds to TorrentCraft {version} for {platform}. The TorrentCraft "
            "application code remains MIT licensed. Qt is used under LGPL-3.0.\n\n"
            "The `qt-source` directory contains the exact upstream Qt source archives; "
            "`vcpkg-ports` records the patches and build recipe; `application-link-material` "
            "contains content-addressed application object files and project static libraries, "
            "with original build paths recorded in `SHA256SUMS.json`; and "
            "`build-metadata` contains the generated build and link descriptions. Rebuild the "
            f"Qt libraries from a modified source tree with the recorded {triplet} triplet and "
            "CMake cache, "
            "then use the recorded path mapping, preserved objects, and generated link "
            "description to relink "
            "`torrentcraft-gui`. Reverse engineering for debugging modifications to the LGPL "
            "library is permitted.\n",
            encoding="utf-8",
        )
        (root / "COMPLIANCE-METADATA.json").write_text(
            json.dumps(
                {
                    "schema": "torrentcraft.qt-lgpl-compliance/v1",
                    "version": version,
                    "platform": platform,
                    "triplet": triplet,
                    "qt_version": manifest["qt_version"],
                    "vcpkg_commit": os.environ.get("VCPKG_COMMIT", "unknown"),
                    "source_commit": source_commit(source),
                },
                indent=2,
                sort_keys=True,
            )
            + "\n",
            encoding="utf-8",
        )
        if platform.startswith("windows"):
            destination = output / f"{root.name}.zip"
            write_zip(root, destination, epoch)
        else:
            destination = output / f"{root.name}.tar.gz"
            write_tar(root, destination, epoch)
    return destination


def checksum_manifest(output: Path) -> None:
    lines = []
    for path in sorted(output.iterdir()):
        if path.is_file() and path.name != "SHA256SUMS":
            lines.append(f"{sha256(path)}  {path.name}")
    (output / "SHA256SUMS").write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-dir", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--vcpkg-root", type=Path, required=True)
    parser.add_argument("--triplet", required=True)
    parser.add_argument("--platform", required=True)
    parser.add_argument("--version", default="1.0.0")
    parser.add_argument("--config", default="")
    parser.add_argument("--include-source", action="store_true")
    args = parser.parse_args()

    source = args.source_dir.resolve()
    build = args.build_dir.resolve()
    output = args.output_dir.resolve()
    vcpkg_root = args.vcpkg_root.resolve()
    output.mkdir(parents=True, exist_ok=True)
    if any(output.iterdir()):
        raise RuntimeError(f"static release output directory must be empty: {output}")
    epoch = source_epoch(source)
    os.environ.setdefault("SOURCE_DATE_EPOCH", str(epoch))

    with tempfile.TemporaryDirectory(prefix="torrentcraft-static-release-") as temporary:
        temporary_root = Path(temporary)
        install = temporary_root / "install"
        command = ["cmake", "--install", str(build), "--prefix", str(install)]
        if args.config:
            command.extend(["--config", args.config])
        if args.platform.startswith("linux"):
            command.append("--strip")
        run(command, source)
        installed = build / "vcpkg_installed" / args.triplet
        if not installed.is_dir():
            raise RuntimeError(f"missing vcpkg installed tree: {installed}")

        extension = ".exe" if args.platform.startswith("windows") else ""
        cli_name = f"TorrentCraft-{args.version}-{args.platform}-cli{extension}"
        gui_name = f"TorrentCraft-{args.version}-{args.platform}-gui{extension}"
        copy_binary(installed_binary(install, "torrentcraft"), output / cli_name)
        copy_binary(installed_binary(install, "torrentcraft-gui"), output / gui_name)

        stem = f"TorrentCraft-{args.version}-{args.platform}"
        sbom = output / f"{stem}.SBOM.spdx.json"
        security = output / f"{stem}.SECURITY-AUDIT.json"
        run(
            [
                sys.executable,
                str(source / "ci" / "release" / "static_sbom.py"),
                "--source-dir",
                str(source),
                "--installed-dir",
                str(installed),
                "--platform",
                args.platform,
                "--vcpkg-root",
                str(vcpkg_root),
                "--version",
                args.version,
                "--include-gui",
                "--output",
                str(sbom),
                "--report",
                str(security),
            ],
            source,
        )
        provenance = {
            "schema": "torrentcraft.release-provenance/v2",
            "version": args.version,
            "platform": args.platform,
            "source_commit": source_commit(source),
            "source_date_epoch": epoch,
            "vcpkg_commit": os.environ.get("VCPKG_COMMIT", "unknown"),
            "triplet": args.triplet,
            "linkage": "static",
            "binaries": {cli_name: sha256(output / cli_name), gui_name: sha256(output / gui_name)},
        }
        (output / f"{stem}.PROVENANCE.json").write_text(
            json.dumps(provenance, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        compliance_archive(
            source,
            build,
            installed,
            output,
            vcpkg_root,
            args.version,
            args.platform,
            args.triplet,
            epoch,
        )

        if args.include_source:
            source_archive = output / f"TorrentCraft-{args.version}-source.tar.gz"
            write_source_archive(
                source,
                source_archive,
                args.version,
                epoch,
            )
            source_security = output / f"TorrentCraft-{args.version}-source.SECURITY-AUDIT.json"
            source_sbom = output / f"TorrentCraft-{args.version}-source.SBOM.spdx.json"
            run(
                [
                    sys.executable,
                    str(source / "ci" / "release" / "security_audit.py"),
                    "--source-dir",
                    str(source),
                    "--output",
                    str(source_security),
                    "--sbom",
                    str(source_sbom),
                ],
                source,
            )
            (output / f"TorrentCraft-{args.version}-source.PROVENANCE.json").write_text(
                json.dumps(
                    {
                        "schema": "torrentcraft.source-provenance/v1",
                        "version": args.version,
                        "source_commit": source_commit(source),
                        "source_date_epoch": epoch,
                        "artifact": source_archive.name,
                        "sha256": sha256(source_archive),
                    },
                    indent=2,
                    sort_keys=True,
                )
                + "\n",
                encoding="utf-8",
            )

    checksum_manifest(output)
    print(f"created static release artifacts in {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
