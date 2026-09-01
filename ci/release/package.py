#!/usr/bin/env python3
"""Create deterministic SDK and application archives."""

from __future__ import annotations

import argparse
import gzip
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile
import zipfile
from pathlib import Path
from typing import Iterable


EXCLUDED_SOURCE_PARTS = {".git", "out", ".vcpkg"}


def run(command: list[str], cwd: Path) -> None:
    subprocess.run(command, cwd=cwd, check=True)


def git_epoch(source: Path) -> int:
    value = subprocess.check_output(
        ["git", "show", "-s", "--format=%ct", "HEAD"], cwd=source, text=True
    ).strip()
    return int(value)


def files_under(root: Path) -> Iterable[Path]:
    for path in sorted(root.rglob("*")):
        if path.is_file() or path.is_symlink():
            yield path


def normalize_tar_info(info: tarfile.TarInfo, epoch: int) -> tarfile.TarInfo:
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
                    info = normalize_tar_info(archive.gettarinfo(str(path), arcname=str(relative)), epoch)
                    if path.is_file() and not path.is_symlink():
                        with path.open("rb") as stream:
                            archive.addfile(info, stream)
                    else:
                        archive.addfile(info)


def write_zip(root: Path, destination: Path, epoch: int) -> None:
    timestamp = max(1980, int(__import__("time").gmtime(epoch).tm_year))
    date_time = (__import__("time").gmtime(epoch).tm_year, __import__("time").gmtime(epoch).tm_mon,
                 __import__("time").gmtime(epoch).tm_mday, __import__("time").gmtime(epoch).tm_hour,
                 __import__("time").gmtime(epoch).tm_min, __import__("time").gmtime(epoch).tm_sec // 2)
    with zipfile.ZipFile(destination, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for path in files_under(root):
            relative = path.relative_to(root.parent)
            info = zipfile.ZipInfo(str(relative), date_time)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            archive.writestr(info, path.read_bytes())


def copy_if_exists(source: Path, destination: Path) -> None:
    if source.is_dir():
        shutil.copytree(source, destination, dirs_exist_ok=True)
    elif source.is_file():
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)


def stage_package(install: Path, source: Path, root: Path, include_gui: bool, include_cli: bool = True, include_sdk: bool = True) -> None:
    root.mkdir(parents=True)
    for name in ("README.md", "LICENSE", "CONTRIBUTING.md"):
        copy_if_exists(source / name, root / name)

    binary = root / "bin"
    if include_cli:
        cli_source = binary_path(install, "torrentcraft")
        require_file(cli_source, "CLI executable")
        copy_if_exists(cli_source, binary / cli_source.name)
    if include_gui:
        gui_source = binary_path(install, "torrentcraft-gui")
        require_file(gui_source, "GUI executable")
        copy_if_exists(gui_source, binary / gui_source.name)
        copy_if_exists(install / "share" / "icons", root / "share" / "icons")
        copy_if_exists(install / "share" / "applications", root / "share" / "applications")

    docs = root / "share" / "doc" / "TorrentUtilsCore"
    copy_if_exists(install / "share" / "doc" / "TorrentUtilsCore", docs)

    if include_sdk:
        sdk = root / "sdk"
        for name in ("include", "lib", "share"):
            copy_if_exists(install / name, sdk / name)


def write_provenance(root: Path, source: Path, version: str, platform: str, epoch: int) -> None:
    provenance = {
        "schema": "torrentcraft.release-provenance/v1",
        "version": version,
        "platform": platform,
        "source_commit": subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=source, text=True
        ).strip(),
        "source_date_epoch": epoch,
        "vcpkg_commit": os.environ.get("VCPKG_COMMIT", "unknown"),
        "build_type": os.environ.get("CMAKE_BUILD_TYPE", "Release"),
    }
    path = root / "share" / "torrentcraft" / "BUILD-METADATA.json"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(provenance, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def source_archive(source: Path, destination: Path, version: str, epoch: int) -> None:
    prefix = f"TorrentCraft-{version}/"
    raw_tar = subprocess.check_output(
        ["git", "archive", "--format=tar", f"--prefix={prefix}", "HEAD"], cwd=source
    )
    with destination.open("wb") as raw:
        with gzip.GzipFile(fileobj=raw, mode="wb", mtime=epoch) as compressed:
            compressed.write(raw_tar)


def checksum_files(directory: Path) -> None:
    entries = []
    for path in sorted(directory.iterdir()):
        if path.name == "SHA256SUMS":
            continue
        if path.is_file():
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
            entries.append(f"{digest}  {path.name}")
    (directory / "SHA256SUMS").write_text("\n".join(entries) + "\n", encoding="utf-8")


def binary_path(install: Path, name: str) -> Path:
    direct = install / "bin" / name
    if direct.is_file():
        return direct
    windows = install / "bin" / f"{name}.exe"
    if windows.is_file():
        return windows
    return direct


def require_file(path: Path, description: str) -> None:
    if not path.is_file():
        raise RuntimeError(f"missing {description}: {path}")



def bundle_linux_gui_runtime(build: Path, root: Path, platform: str) -> None:
    if not platform.startswith("linux"):
        return
    install_roots = list((build / "vcpkg_installed").glob("*"))
    library_destination = root / "lib"
    plugin_destination = library_destination / "qt6" / "plugins"
    qt_libraries = 0
    for installed in install_roots:
        for library in installed.rglob("libQt6*.so*"):
            copy_if_exists(library, library_destination / library.name)
            qt_libraries += 1
        for plugin_root in installed.rglob("plugins"):
            if (plugin_root / "platforms").is_dir():
                shutil.copytree(plugin_root, plugin_destination, dirs_exist_ok=True)
    if qt_libraries == 0:
        raise RuntimeError("Linux GUI package has no Qt6 shared libraries to bundle")
    (root / "bin" / "qt.conf").write_text(
        "[Paths]\nPrefix=..\nPlugins=../lib/qt6/plugins\n", encoding="utf-8"
    )
def write_runtime_manifest(root: Path, platform: str) -> None:
    report: dict[str, object] = {
        "schema": "torrentcraft.runtime-dependencies/v1",
        "platform": platform,
        "binaries": {},
    }
    for binary in sorted((root / "bin").glob("*")):
        if not binary.is_file():
            continue
        if platform.startswith("linux") and shutil.which("ldd"):
            result = subprocess.run(["ldd", str(binary)], capture_output=True, text=True, check=False)
            dependencies: object = [
                line.split(" (0x", 1)[0].rstrip() for line in result.stdout.splitlines()
            ]
            tool = "ldd"
        else:
            dependencies = ["Windows loader dependencies are validated by the Windows CI image"]
            tool = "windows-loader"
        report["binaries"][str(binary.relative_to(root))] = {
            "tool": tool,
            "dependencies": dependencies,
        }
    destination = root / "share" / "torrentcraft" / "RUNTIME-DEPENDENCIES.json"
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def copy_security_artifacts(root: Path, report: Path, sbom: Path) -> None:
    destination = root / "share" / "torrentcraft"
    destination.mkdir(parents=True, exist_ok=True)
    copy_if_exists(report, destination / "SECURITY-AUDIT.json")
    copy_if_exists(sbom, destination / "SBOM.spdx.json")
    audit = json.loads(report.read_text(encoding="utf-8"))
    license_inventory = {
        "schema": "torrentcraft.license-inventory/v1",
        "project_license": "MIT",
        "vcpkg_baseline": audit.get("vcpkg_baseline", "unknown"),
        "dependencies": audit.get("manifest_dependencies", []),
    }
    (destination / "LICENSES.json").write_text(
        json.dumps(license_inventory, indent=2, sort_keys=True) + "\n", encoding="utf-8")
def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-dir", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--version", default="1.0.0")
    parser.add_argument("--platform", required=True)
    parser.add_argument("--include-gui", action="store_true")
    parser.add_argument("--config", default="")
    parser.add_argument("--source-date-epoch", type=int)
    args = parser.parse_args()

    source = args.source_dir.resolve()
    build = args.build_dir.resolve()
    output = args.output_dir.resolve()
    epoch = args.source_date_epoch if args.source_date_epoch is not None else git_epoch(source)
    output.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="torrentcraft-release-package-") as temporary:
        temporary_root = Path(temporary)
        install = temporary_root / "install"
        install_command = ["cmake", "--install", str(build), "--prefix", str(install)]
        if args.config:
            install_command.extend(["--config", args.config])
        security_report = temporary_root / "SECURITY-AUDIT.json"
        security_sbom = temporary_root / "SBOM.spdx.json"
        run(
            [sys.executable, str(source / "ci/release/security_audit.py"), "--source-dir", str(source),
             "--output", str(security_report), "--sbom", str(security_sbom)],
            source,
        )
        run(install_command, source)

        package_name = f"TorrentCraft-{args.version}-{args.platform}"
        application_root = temporary_root / package_name
        stage_package(install, source, application_root, args.include_gui, include_sdk=False)
        if args.include_gui:
            bundle_linux_gui_runtime(build, application_root, args.platform)
        write_runtime_manifest(application_root, args.platform)
        copy_security_artifacts(application_root, security_report, security_sbom)
        write_provenance(application_root, source, args.version, args.platform, epoch)

        if sys.platform == "win32" or args.platform.startswith("windows"):
            write_zip(application_root, output / f"{package_name}.zip", epoch)
        else:
            write_tar(application_root, output / f"{package_name}.tar.gz", epoch)

        sdk_root = temporary_root / f"TorrentUtils-SDK-{args.version}-{args.platform}"
        stage_package(install, source, sdk_root, False, False, include_sdk=True)
        sdk_archive = output / f"TorrentUtils-SDK-{args.version}-{args.platform}"
        write_provenance(sdk_root, source, args.version, args.platform, epoch)
        copy_security_artifacts(sdk_root, security_report, security_sbom)
        if sys.platform == "win32" or args.platform.startswith("windows"):
            write_zip(sdk_root, Path(str(sdk_archive) + ".zip"), epoch)
        else:
            write_tar(sdk_root, Path(str(sdk_archive) + ".tar.gz"), epoch)
        copy_if_exists(security_report, output / f"TorrentCraft-{args.version}-source.SECURITY-AUDIT.json")
        copy_if_exists(security_sbom, output / f"TorrentCraft-{args.version}-source.SBOM.spdx.json")

    source_archive(source, output / f"TorrentCraft-{args.version}-source.tar.gz", args.version, epoch)
    checksum_files(output)
    print(f"created release artifacts in {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
