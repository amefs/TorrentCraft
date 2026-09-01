#!/usr/bin/env python3
"""Validate the static release contract."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import tarfile
import zipfile
from pathlib import Path


WINDOWS_SYSTEM_DLLS = {
    "AUTHZ.DLL",
    "ADVAPI32.DLL",
    "BCRYPT.DLL",
    "CABINET.DLL",
    "CFGMGR32.DLL",
    "COMCTL32.DLL",
    "COMDLG32.DLL",
    "CRYPT32.DLL",
    "D3D11.DLL",
    "D3D12.DLL",
    "D3D9.DLL",
    "DCOMP.DLL",
    "DNSAPI.DLL",
    "DSOUND.DLL",
    "DWRITE.DLL",
    "DWMAPI.DLL",
    "DXGI.DLL",
    "DXVA2.DLL",
    "GDI32.DLL",
    "IMM32.DLL",
    # Windows ships the legacy ICU common/i18n DLLs since version 1703 and
    # the combined ICU DLL since version 1903. They are OS dependencies, not
    # redistributable vcpkg runtime dependencies.
    "ICU.DLL",
    "ICUIN.DLL",
    "ICUUC.DLL",
    "IPHLPAPI.DLL",
    "KERNEL32.DLL",
    "MPR.DLL",
    "MSIMG32.DLL",
    "MSWSOCK.DLL",
    "NETAPI32.DLL",
    "NCRYPT.DLL",
    "NTDLL.DLL",
    "NORMALIZ.DLL",
    "OLEACC.DLL",
    "OLE32.DLL",
    "OLEAUT32.DLL",
    "OPENGL32.DLL",
    "POWRPROF.DLL",
    "PROPSYS.DLL",
    "RPCRT4.DLL",
    "SECUR32.DLL",
    "SETUPAPI.DLL",
    "SHCORE.DLL",
    "SHELL32.DLL",
    "SHLWAPI.DLL",
    "USER32.DLL",
    "UIAUTOMATIONCORE.DLL",
    "USERENV.DLL",
    "UXTHEME.DLL",
    "VERSION.DLL",
    "WINMM.DLL",
    "WINHTTP.DLL",
    "WININET.DLL",
    "WINCODEC.DLL",
    "WINTRUST.DLL",
    "WLANAPI.DLL",
    "WINSPOOL.DRV",
    "WS2_32.DLL",
    "WTSAPI32.DLL",
}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def verify_checksums(directory: Path) -> None:
    manifest = directory / "SHA256SUMS"
    if not manifest.is_file():
        raise RuntimeError(f"missing checksum manifest: {manifest}")
    seen: set[str] = set()
    for line in manifest.read_text(encoding="utf-8").splitlines():
        digest, name = line.split("  ", 1)
        path = directory / name
        if not path.is_file() or sha256(path) != digest:
            raise RuntimeError(f"checksum mismatch: {name}")
        seen.add(name)
    expected = {
        path.name
        for path in directory.iterdir()
        if path.is_file() and path.name != "SHA256SUMS"
    }
    if seen != expected:
        raise RuntimeError(
            f"checksum manifest mismatch: expected={sorted(expected)} seen={sorted(seen)}"
        )


def capture(command: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, capture_output=True, text=True, check=False)


def ldd_reports_dependencies(output: str) -> bool:
    normalized = output.lower()
    static_diagnostics = (
        "not a dynamic executable",
        "not a valid dynamic program",
        "statically linked",
    )
    if any(diagnostic in normalized for diagnostic in static_diagnostics):
        return False
    return "=>" in normalized or bool(
        re.search(r"\b(lib|ld-musl|ld-linux)[^\s]*\.so", normalized)
    )


def scan_linux_static(binary: Path) -> None:
    readelf = shutil.which("readelf")
    file_tool = shutil.which("file")
    if not readelf or not file_tool:
        raise RuntimeError("Linux static validation requires readelf and file")
    program_headers = capture([readelf, "-lW", str(binary)])
    if program_headers.returncode != 0:
        raise RuntimeError(program_headers.stderr.strip())
    if re.search(r"\bINTERP\b|Requesting program interpreter", program_headers.stdout):
        raise RuntimeError(f"{binary.name} contains an ELF program interpreter")
    dynamic = capture([readelf, "-dW", str(binary)])
    if dynamic.returncode != 0:
        raise RuntimeError(dynamic.stderr.strip())
    if re.search(r"\(NEEDED\)", dynamic.stdout):
        raise RuntimeError(f"{binary.name} contains DT_NEEDED shared-library dependencies")
    kind = capture([file_tool, "-L", str(binary)])
    if kind.returncode != 0 or not re.search(r"static(?:ally|-pie)", kind.stdout, re.IGNORECASE):
        raise RuntimeError(f"{binary.name} is not reported as static: {kind.stdout.strip()}")
    if "not stripped" in kind.stdout.lower() or not re.search(
        r"\bstripped\b", kind.stdout, re.IGNORECASE
    ):
        raise RuntimeError(f"{binary.name} is not stripped: {kind.stdout.strip()}")
    ldd = shutil.which("ldd")
    if ldd:
        result = capture([ldd, str(binary)])
        output = result.stdout + result.stderr
        if ldd_reports_dependencies(output):
            raise RuntimeError(f"{binary.name} still has loader dependencies: {output.strip()}")


def windows_dependencies(binary: Path) -> set[str]:
    dumpbin = shutil.which("dumpbin") or shutil.which("dumpbin.exe")
    if not dumpbin:
        raise RuntimeError("Windows static validation requires dumpbin.exe")
    result = capture([dumpbin, "/nologo", "/dependents", str(binary)])
    if result.returncode != 0:
        raise RuntimeError(result.stdout + result.stderr)
    dependencies = {
        match.group(1).upper()
        for match in re.finditer(r"(?im)^\s*([A-Za-z0-9_.-]+\.(?:dll|drv))\s*$", result.stdout)
    }
    if not dependencies:
        raise RuntimeError(f"dumpbin did not report dependencies for {binary.name}")
    return dependencies


def scan_windows_static(binary: Path) -> None:
    dependencies = windows_dependencies(binary)
    unexpected = sorted(
        dependency
        for dependency in dependencies
        if dependency not in WINDOWS_SYSTEM_DLLS
        and not dependency.startswith("API-MS-WIN-")
        and not dependency.startswith("EXT-MS-WIN-")
    )
    if unexpected:
        raise RuntimeError(f"{binary.name} imports non-system DLLs: {unexpected}")
    forbidden = [
        dependency
        for dependency in dependencies
        if dependency.startswith(("VCRUNTIME", "MSVCP", "QT6", "LIBCRYPTO", "LIBSSL"))
        or dependency in {"UCRTBASE.DLL", "LIBGCC_S_SEH-1.DLL", "LIBSTDC++-6.DLL"}
    ]
    if forbidden:
        raise RuntimeError(f"{binary.name} imports forbidden runtimes: {sorted(forbidden)}")


def run_smoke(cli: Path, gui: Path, platform: str) -> None:
    subprocess.run(
        [str(cli), "--help"],
        check=True,
        timeout=30,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    environment = dict(os.environ)
    environment["TORRENTCRAFT_GUI_SMOKE"] = "1"
    if platform.startswith("linux"):
        environment["QT_QPA_PLATFORM"] = "xcb"
    if platform.startswith("linux-musl"):
        # Exercise the host-module isolation required by the fully static GTK build.
        environment["GTK_MODULES"] = "torrentcraft-static-smoke-module"
        environment["GTK3_MODULES"] = "torrentcraft-static-smoke-module"
    completed = subprocess.run(
        [str(gui)],
        check=True,
        timeout=30,
        env=environment,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
        text=True,
    )
    if platform.startswith("linux-musl") and any(
        marker in completed.stderr
        for marker in ("GModule-CRITICAL", "Failed to load module")
    ):
        raise RuntimeError(
            f"{gui.name} attempted to load dynamic GTK modules: {completed.stderr.strip()}"
        )


def archive_names(path: Path) -> list[str]:
    if path.name.endswith(".tar.gz"):
        with tarfile.open(path, "r:gz") as archive:
            return archive.getnames()
    if path.suffix == ".zip":
        with zipfile.ZipFile(path) as archive:
            return archive.namelist()
    raise RuntimeError(f"unsupported archive: {path}")


def validate_compliance(directory: Path, version: str, platform: str) -> Path:
    candidates = list(
        directory.glob(f"TorrentCraft-{version}-{platform}-Qt-LGPL-compliance.*")
    )
    candidates = [path for path in candidates if path.name.endswith((".tar.gz", ".zip"))]
    if len(candidates) != 1:
        raise RuntimeError("expected exactly one Qt LGPL compliance archive")
    names = archive_names(candidates[0])
    required_suffixes = {
        "RELINKING.md",
        "COMPLIANCE-METADATA.json",
        "qt-source/qtbase-everywhere-src-6.11.1.tar.xz",
        "qt-source/qtsvg-everywhere-src-6.11.1.tar.xz",
        "licenses/qt/LGPL-3.0-only.txt",
        "application-link-material/SHA256SUMS.json",
        "build-metadata/CMakeCache.txt",
    }
    for suffix in required_suffixes:
        if not any(name.endswith(suffix) for name in names):
            raise RuntimeError(f"LGPL compliance archive missing {suffix}")
    if not any(name.endswith((".o", ".obj")) for name in names):
        raise RuntimeError("LGPL compliance archive has no application object files")
    if not any(name.endswith((".a", ".lib")) for name in names):
        raise RuntimeError("LGPL compliance archive has no project static libraries")
    if platform.startswith("windows"):
        if not any(name.endswith(".vcxproj") for name in names):
            raise RuntimeError("LGPL compliance archive has no MSVC link description")
    elif not any(name.endswith("build.ninja") for name in names):
        raise RuntimeError("LGPL compliance archive has no Ninja link description")
    return candidates[0]


def validate_spdx(path: Path, platform: str) -> None:
    document = json.loads(path.read_text(encoding="utf-8"))
    if document.get("spdxVersion") != "SPDX-2.3":
        raise RuntimeError(f"invalid SPDX version in {path.name}")
    names = {package.get("name") for package in document.get("packages", [])}
    required = {"TorrentCraft", "libtorrent", "qtbase", "qtsvg"}
    if platform.startswith("linux"):
        required.update({"fontconfig", "gtk3"})
    if not required.issubset(names):
        raise RuntimeError(f"{path.name} missing static packages: {sorted(required - names)}")


def validate_file_set(
    directory: Path,
    version: str,
    platform: str,
    include_source: bool,
    compliance: Path,
) -> None:
    extension = ".exe" if platform.startswith("windows") else ""
    stem = f"TorrentCraft-{version}-{platform}"
    expected = {
        "SHA256SUMS",
        f"{stem}-cli{extension}",
        f"{stem}-gui{extension}",
        f"{stem}.PROVENANCE.json",
        f"{stem}.SECURITY-AUDIT.json",
        f"{stem}.SBOM.spdx.json",
        compliance.name,
    }
    if include_source:
        expected.update(
            {
                f"TorrentCraft-{version}-source.tar.gz",
                f"TorrentCraft-{version}-source.PROVENANCE.json",
                f"TorrentCraft-{version}-source.SECURITY-AUDIT.json",
                f"TorrentCraft-{version}-source.SBOM.spdx.json",
            }
        )
    actual = {path.name for path in directory.iterdir()}
    if actual != expected:
        raise RuntimeError(
            f"release file set mismatch: expected={sorted(expected)} actual={sorted(actual)}"
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--platform", required=True)
    parser.add_argument("--version", default="1.0.0")
    parser.add_argument("--include-source", action="store_true")
    args = parser.parse_args()

    directory = args.directory.resolve()
    verify_checksums(directory)
    extension = ".exe" if args.platform.startswith("windows") else ""
    cli = directory / f"TorrentCraft-{args.version}-{args.platform}-cli{extension}"
    gui = directory / f"TorrentCraft-{args.version}-{args.platform}-gui{extension}"
    if not cli.is_file() or not gui.is_file():
        raise RuntimeError("release must contain exactly one bare CLI and GUI executable")
    if args.platform.startswith("linux"):
        scan_linux_static(cli)
        scan_linux_static(gui)
    else:
        scan_windows_static(cli)
        scan_windows_static(gui)
    run_smoke(cli, gui, args.platform)

    stem = f"TorrentCraft-{args.version}-{args.platform}"
    for suffix in ("PROVENANCE.json", "SECURITY-AUDIT.json", "SBOM.spdx.json"):
        path = directory / f"{stem}.{suffix}"
        if not path.is_file():
            raise RuntimeError(f"missing release metadata: {path.name}")
    validate_spdx(directory / f"{stem}.SBOM.spdx.json", args.platform)
    compliance = validate_compliance(directory, args.version, args.platform)

    forbidden = list(directory.glob("TorrentUtils-SDK-*"))
    if forbidden:
        raise RuntimeError(f"SDK artifacts are forbidden in the static-only release: {forbidden}")
    if args.include_source:
        for suffix in (
            "source.tar.gz",
            "source.PROVENANCE.json",
            "source.SBOM.spdx.json",
            "source.SECURITY-AUDIT.json",
        ):
            path = directory / f"TorrentCraft-{args.version}-{suffix}"
            if not path.is_file():
                raise RuntimeError(f"missing source sidecar: {path.name}")
    validate_file_set(
        directory,
        args.version,
        args.platform,
        args.include_source,
        compliance,
    )
    print(f"validated static release in {directory}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
