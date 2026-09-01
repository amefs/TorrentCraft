#!/usr/bin/env python3
"""Validate release archives, checksums, install-tree contents, and smoke runs."""

from __future__ import annotations

import argparse
import hashlib
import os
import subprocess
import tarfile
import tempfile
import zipfile
from pathlib import Path


def verify_checksums(directory: Path) -> None:
    sums = directory / "SHA256SUMS"
    if not sums.is_file():
        raise RuntimeError(f"missing checksum file: {sums}")
    seen: set[str] = set()
    for line in sums.read_text(encoding="utf-8").splitlines():
        digest, name = line.split("  ", 1)
        path = directory / name
        if not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != digest:
            raise RuntimeError(f"checksum mismatch: {name}")
        seen.add(name)
    expected = {path.name for path in directory.iterdir() if path.is_file() and path.name != "SHA256SUMS"}
    if seen != expected:
        raise RuntimeError(f"checksum manifest mismatch: expected={sorted(expected)} seen={sorted(seen)}")


def archive_names(path: Path) -> list[str]:
    if path.name.endswith(".tar.gz"):
        with tarfile.open(path, "r:gz") as archive:
            return archive.getnames()
    if path.suffix == ".zip":
        with zipfile.ZipFile(path) as archive:
            return archive.namelist()
    raise RuntimeError(f"unsupported archive: {path}")


def archive_root(names: list[str]) -> str:
    if not names:
        raise RuntimeError("archive is empty")
    return names[0].split("/", 1)[0]


def require_members(archive: Path, expected: list[str]) -> None:
    names = set(archive_names(archive))
    missing = [name for name in expected if name not in names]
    if missing:
        raise RuntimeError(f"{archive.name} missing members: {missing}")


def require_any_member(archive: Path, candidates: list[str]) -> None:
    names = set(archive_names(archive))
    if not any(candidate in names for candidate in candidates):
        raise RuntimeError(f"{archive.name} missing all candidates: {candidates}")
def require_prefix(archive: Path, prefix: str) -> None:
    if not any(name.startswith(prefix) for name in archive_names(archive)):
        raise RuntimeError(f"{archive.name} missing member prefix: {prefix}")



def extract_archive(archive: Path, destination: Path) -> Path:
    names = archive_names(archive)
    for name in names:
        parts = Path(name).parts
        if Path(name).is_absolute() or ".." in parts:
            raise RuntimeError(f"unsafe archive member: {name}")
    if archive.name.endswith(".tar.gz"):
        with tarfile.open(archive, "r:gz") as opened:
            opened.extractall(destination)
    else:
        with zipfile.ZipFile(archive) as opened:
            opened.extractall(destination)
    return destination / archive_root(names)


def executable_path(root: Path, name: str) -> Path:
    for candidate in (root / "bin" / name, root / "bin" / f"{name}.exe"):
        if candidate.is_file():
            return candidate
    raise RuntimeError(f"missing executable in archive: {name}")


def run_smoke(archive: Path, include_gui: bool) -> None:
    with tempfile.TemporaryDirectory(prefix="torrentcraft-release-smoke-") as temporary:
        root = extract_archive(archive, Path(temporary))
        environment = dict(os.environ)
        environment.setdefault("QT_QPA_PLATFORM", "offscreen")
        binaries = ["torrentcraft"] + (["torrentcraft-gui"] if include_gui else [])
        for name in binaries:
            subprocess.run(
                [str(executable_path(root, name)), "--help"],
                cwd=root,
                env=environment,
                check=True,
                timeout=30,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )


def validate_application(archive: Path, include_gui: bool) -> None:
    base = archive_root(archive_names(archive))
    expected = [
        f"{base}/share/torrentcraft/BUILD-METADATA.json",
        f"{base}/share/torrentcraft/RUNTIME-DEPENDENCIES.json",
        f"{base}/share/torrentcraft/SECURITY-AUDIT.json",
        f"{base}/share/torrentcraft/LICENSES.json",
        f"{base}/share/torrentcraft/SBOM.spdx.json",
    ]
    if include_gui:
        expected.append(f"{base}/bin/qt.conf")
    require_members(archive, expected)
    require_any_member(
        archive,
        [f"{base}/bin/torrentcraft", f"{base}/bin/torrentcraft.exe"],
    )
    if include_gui:
        require_any_member(
            archive,
            [f"{base}/bin/torrentcraft-gui", f"{base}/bin/torrentcraft-gui.exe"],
        )
    if include_gui and "linux" in archive.name:
        require_prefix(archive, f"{base}/lib/libQt6")
    run_smoke(archive, include_gui)


def validate_sdk(archive: Path) -> None:
    base = archive_root(archive_names(archive))
    require_members(
        archive,
        [
            f"{base}/sdk/include/torrentutils/core/core.hpp",
            f"{base}/sdk/lib/cmake/TorrentUtilsCore/TorrentUtilsCoreConfig.cmake",
            f"{base}/share/torrentcraft/SECURITY-AUDIT.json",
            f"{base}/share/torrentcraft/LICENSES.json",
            f"{base}/share/torrentcraft/SBOM.spdx.json",
        ],
    )
    require_any_member(
        archive,
        [f"{base}/sdk/lib/libTorrentUtilsCore.a", f"{base}/sdk/lib/TorrentUtilsCore.lib"],
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--platform", required=True)
    parser.add_argument("--include-gui", action="store_true")
    args = parser.parse_args()

    directory = args.directory.resolve()
    verify_checksums(directory)
    application = list(directory.glob(f"TorrentCraft-*-{args.platform}.tar.gz"))
    application += list(directory.glob(f"TorrentCraft-*-{args.platform}.zip"))
    sdk = list(directory.glob(f"TorrentUtils-SDK-*-{args.platform}.tar.gz"))
    sdk += list(directory.glob(f"TorrentUtils-SDK-*-{args.platform}.zip"))
    source = list(directory.glob("TorrentCraft-*-source.tar.gz"))
    if len(application) != 1 or len(sdk) != 1 or len(source) != 1:
        raise RuntimeError("expected exactly one application, SDK, and source archive")
    validate_application(application[0], args.include_gui)
    validate_sdk(sdk[0])
    source_root = archive_root(archive_names(source[0]))
    require_members(source[0], [f"{source_root}/README.md", f"{source_root}/CMakeLists.txt"])
    print(f"Release artifacts validated in {directory}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
