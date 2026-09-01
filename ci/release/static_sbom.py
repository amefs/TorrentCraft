#!/usr/bin/env python3
"""Create an SPDX inventory for the packages in a static release link closure."""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
import re
import subprocess
import sys
from pathlib import Path

# The Windows embeddable Python distribution enables _pth isolation and does
# not automatically add the executed script directory to sys.path.
script_directory = Path(__file__).resolve().parent
if str(script_directory) not in sys.path:
    sys.path.insert(0, str(script_directory))

from security_audit import public_header_findings


BASE_ROOTS = ("indicators", "libtorrent", "nlohmann-json")
GUI_ROOTS = ("qtbase", "qtsvg")
BUILD_ONLY_PACKAGES = {"boost-cmake", "boost-uninstall"}


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


def parse_control(
    path: Path, architecture: str | None = None
) -> dict[str, dict[str, object]]:
    if not path.is_file():
        raise RuntimeError(f"missing vcpkg status database: {path}")
    records: dict[str, dict[str, object]] = {}
    for paragraph in re.split(r"\n\s*\n", path.read_text(encoding="utf-8")):
        fields: dict[str, str] = {}
        current = ""
        for line in paragraph.splitlines():
            if line[:1].isspace() and current:
                fields[current] += " " + line.strip()
                continue
            if ":" not in line:
                continue
            current, value = line.split(":", 1)
            fields[current] = value.strip()
        name = fields.get("Package", "")
        if not name or fields.get("Status") != "install ok installed":
            continue
        if architecture is not None and fields.get("Architecture") != architecture:
            continue
        record = records.setdefault(
            name,
            {"name": name, "version": fields.get("Version", "unknown"), "depends": set()},
        )
        for item in fields.get("Depends", "").split(","):
            candidate = item.split("|", 1)[0].strip()
            candidate = re.sub(r"\s*\([^)]*\)", "", candidate)
            candidate = re.sub(r"\[[^]]*\]", "", candidate)
            candidate = candidate.split(":", 1)[0]
            if candidate:
                record["depends"].add(candidate)
    return records


def is_build_only_package(name: str) -> bool:
    return name.startswith("vcpkg-") or name in BUILD_ONLY_PACKAGES


def is_unconditionally_empty_port(portfile: Path) -> bool:
    if not portfile.is_file():
        return False
    commands = [
        line.strip()
        for line in portfile.read_text(encoding="utf-8").splitlines()
        if line.strip() and not line.lstrip().startswith("#")
    ]
    return commands == ["set(VCPKG_POLICY_EMPTY_PACKAGE enabled)"]


def dependency_closure(
    records: dict[str, dict[str, object]], roots: tuple[str, ...]
) -> list[dict[str, object]]:
    missing = [name for name in roots if name not in records]
    if missing:
        raise RuntimeError(f"static dependency roots missing from vcpkg status: {missing}")
    selected: set[str] = set()
    pending = list(roots)
    while pending:
        name = pending.pop()
        if name in selected or is_build_only_package(name):
            continue
        selected.add(name)
        for dependency in records[name]["depends"]:
            if dependency in records and dependency not in selected:
                pending.append(dependency)
    return [records[name] for name in sorted(selected)]


def port_license(source: Path, vcpkg_root: Path | None, name: str) -> str:
    manifests = [source / "cmake" / "vcpkg-ports" / name / "vcpkg.json"]
    if vcpkg_root is not None:
        manifests.append(vcpkg_root / "ports" / name / "vcpkg.json")
    for manifest in manifests:
        if not manifest.is_file():
            continue
        value = json.loads(manifest.read_text(encoding="utf-8")).get("license")
        if isinstance(value, str) and value.strip():
            return value.strip()
        if is_unconditionally_empty_port(manifest.with_name("portfile.cmake")):
            return "NONE"
    return "NOASSERTION"


def license_override(source: Path, installed: Path, name: str, version: str) -> str:
    manifest = json.loads(
        (source / "ci" / "release" / "license-overrides.json").read_text(encoding="utf-8")
    )
    override = manifest.get(name)
    if not isinstance(override, dict):
        return "NOASSERTION"
    if override.get("version") != version:
        raise RuntimeError(f"license override version mismatch for {name} {version}")
    copyright_file = installed / "share" / name / "copyright"
    if not copyright_file.is_file():
        raise RuntimeError(f"license override evidence is missing for {name}")
    actual = hashlib.sha256(copyright_file.read_bytes()).hexdigest()
    if actual != override.get("copyright_sha256"):
        raise RuntimeError(f"license override evidence hash mismatch for {name}")
    return str(override["license"])


def package_license(
    source: Path,
    installed: Path,
    vcpkg_root: Path | None,
    name: str,
    version: str,
) -> str:
    spdx = installed / "share" / name / "vcpkg.spdx.json"
    expressions: list[str] = []
    if spdx.is_file():
        document = json.loads(spdx.read_text(encoding="utf-8"))
        for package in document.get("packages", []):
            expression = package.get("licenseConcluded", "NOASSERTION")
            if expression == "NOASSERTION":
                expression = package.get("licenseDeclared", "NOASSERTION")
            if expression != "NOASSERTION" and expression not in expressions:
                expressions.append(expression)
    if expressions:
        return " AND ".join(f"({value})" for value in expressions)
    declared = port_license(source, vcpkg_root, name)
    return (
        declared
        if declared != "NOASSERTION"
        else license_override(source, installed, name, version)
    )


def spdx_id(name: str) -> str:
    return "SPDXRef-" + re.sub(r"[^A-Za-z0-9.-]", "-", name)


def is_gpl_only(expression: str) -> bool:
    tokens = re.findall(r"[()]|[A-Za-z0-9.+:-]+", expression)
    if not tokens or re.sub(r"[()]|[A-Za-z0-9.+:-]+", "", expression).strip():
        raise RuntimeError(f"invalid SPDX license expression: {expression}")
    position = 0

    def peek() -> str | None:
        return tokens[position] if position < len(tokens) else None

    def consume() -> str:
        nonlocal position
        if position >= len(tokens):
            raise RuntimeError(f"incomplete SPDX license expression: {expression}")
        token = tokens[position]
        position += 1
        return token

    def parse_primary() -> bool:
        token = consume()
        if token == "(":
            restricted = parse_or()
            if consume() != ")":
                raise RuntimeError(f"unbalanced SPDX license expression: {expression}")
            return restricted
        if token == ")" or token.upper() in {"AND", "OR", "WITH"}:
            raise RuntimeError(f"invalid SPDX license expression: {expression}")
        upper = token.upper()
        return "GPL-" in upper and "LGPL-" not in upper

    def parse_with() -> bool:
        restricted = parse_primary()
        if (peek() or "").upper() == "WITH":
            consume()
            exception = consume().upper()
            if exception == "QT-GPL-EXCEPTION-1.0":
                restricted = False
        return restricted

    def parse_and() -> bool:
        restricted = parse_with()
        while (peek() or "").upper() == "AND":
            consume()
            right = parse_with()
            restricted = restricted or right
        return restricted

    def parse_or() -> bool:
        restricted = parse_and()
        while (peek() or "").upper() == "OR":
            consume()
            right = parse_and()
            restricted = restricted and right
        return restricted

    restricted = parse_or()
    if position != len(tokens):
        raise RuntimeError(f"invalid SPDX license expression: {expression}")
    return restricted


def create_documents(
    source: Path,
    installed: Path,
    platform: str,
    version: str,
    include_gui: bool,
    vcpkg_root: Path | None,
) -> tuple[dict[str, object], dict[str, object]]:
    status_path = installed.parent / "vcpkg" / "status"
    records = parse_control(status_path, installed.name)
    roots = BASE_ROOTS + (GUI_ROOTS if include_gui else ())
    components = dependency_closure(records, roots)
    for component in components:
        component["license"] = package_license(
            source,
            installed,
            vcpkg_root,
            str(component["name"]),
            str(component["version"]),
        )
        component["depends"] = sorted(component["depends"])

    qt_manifest = json.loads(
        (source / "ci" / "release" / "qt-lgpl-sources.json").read_text(encoding="utf-8")
    )
    if include_gui:
        actual_qt = str(records["qtbase"]["version"]).split("#", 1)[0]
        if actual_qt != qt_manifest["qt_version"]:
            raise RuntimeError(
                f"Qt LGPL source manifest is {qt_manifest['qt_version']}, "
                f"installed qtbase is {actual_qt}"
            )

    commit = source_commit(source)
    created = dt.datetime.fromtimestamp(source_epoch(source), tz=dt.timezone.utc).strftime(
        "%Y-%m-%dT%H:%M:%SZ"
    )
    project_id = "SPDXRef-TorrentCraft"
    packages: list[dict[str, object]] = [
        {
            "SPDXID": project_id,
            "name": "TorrentCraft",
            "versionInfo": version,
            "downloadLocation": "NOASSERTION",
            "filesAnalyzed": False,
            "licenseConcluded": "MIT",
            "licenseDeclared": "MIT",
            "supplier": "Organization: TorrentCraft contributors",
        }
    ]
    relationships: list[dict[str, str]] = []
    for component in components:
        name = str(component["name"])
        component_id = spdx_id(name)
        packages.append(
            {
                "SPDXID": component_id,
                "name": name,
                "versionInfo": str(component["version"]),
                "downloadLocation": "NOASSERTION",
                "filesAnalyzed": False,
                "licenseConcluded": str(component["license"]),
                "licenseDeclared": str(component["license"]),
                "externalRefs": [
                    {
                        "referenceCategory": "PACKAGE-MANAGER",
                        "referenceType": "purl",
                        "referenceLocator": f"pkg:vcpkg/{name}@{component['version']}",
                    }
                ],
            }
        )
        relationships.append(
            {
                "spdxElementId": project_id,
                "relationshipType": "STATIC_LINK",
                "relatedSpdxElement": component_id,
            }
        )
    document = {
        "spdxVersion": "SPDX-2.3",
        "dataLicense": "CC0-1.0",
        "SPDXID": "SPDXRef-DOCUMENT",
        "name": f"TorrentCraft {version} {platform} static release",
        "documentNamespace": "https://torrentcraft.invalid/spdx/"
        + hashlib.sha256(f"{commit}:{platform}".encode()).hexdigest(),
        "creationInfo": {
            "created": created,
            "creators": ["Tool: torrentcraft-release-static-sbom"],
        },
        "packages": packages,
        "relationships": relationships,
    }
    license_findings = []
    for component in components:
        expression = str(component["license"])
        reason = ""
        if expression == "NOASSERTION":
            reason = "static dependency has no resolvable SPDX license expression"
        elif is_gpl_only(expression):
            reason = "GPL-only dependency cannot enter an MIT/LGPL static release"
        if reason:
            license_findings.append(
                {
                    "package": component["name"],
                    "license": expression,
                    "reason": reason,
                }
            )
    report = {
        "schema": "torrentcraft.release-static-security/v2",
        "commit": commit,
        "platform": platform,
        "inventory_source": str(status_path.relative_to(source))
        if status_path.is_relative_to(source)
        else str(status_path),
        "static_dependency_roots": list(roots),
        "static_dependencies": components,
        "public_header_findings": public_header_findings(source),
        "license_findings": license_findings,
        "toolchain": {
            "cc": os.environ.get("CC", "default"),
            "cxx": os.environ.get("CXX", "default"),
            "vcpkg_commit": os.environ.get("VCPKG_COMMIT", "unknown"),
        },
    }
    return report, document


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-dir", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--installed-dir", type=Path, required=True)
    parser.add_argument("--platform", required=True)
    parser.add_argument("--vcpkg-root", type=Path)
    parser.add_argument("--version", default="1.0.0")
    parser.add_argument("--include-gui", action="store_true")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()

    source = args.source_dir.resolve()
    report, document = create_documents(
        source,
        args.installed_dir.resolve(),
        args.platform,
        args.version,
        args.include_gui,
        args.vcpkg_root.resolve() if args.vcpkg_root else None,
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(document, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    args.report.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    findings = report["public_header_findings"] + report["license_findings"]
    if findings:
        for finding in findings:
            print(json.dumps(finding, sort_keys=True))
        return 1
    print(f"static SBOM contains {len(report['static_dependencies'])} dependency packages")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
