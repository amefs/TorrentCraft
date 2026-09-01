#!/usr/bin/env python3
"""Run dependency, license, SBOM and public-header checks for a release."""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
import re
import subprocess
from pathlib import Path


FORBIDDEN_PUBLIC_HEADERS = re.compile(
    r"#\s*include\s*[<\"](?:boost/|libtorrent/|Qt|nlohmann/|bencode)"
)


def source_commit(source: Path) -> str:
    ci_commit = os.environ.get("CI_COMMIT_SHA")
    if ci_commit:
        return ci_commit
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


def public_header_findings(source: Path) -> list[dict[str, object]]:
    findings = []
    for header in sorted((source / "include").rglob("*.hpp")):
        for line_number, line in enumerate(header.read_text(encoding="utf-8").splitlines(), 1):
            if FORBIDDEN_PUBLIC_HEADERS.search(line):
                findings.append(
                    {"file": str(header.relative_to(source)), "line": line_number, "text": line}
                )
    return findings


def manifest_components(source: Path) -> list[dict[str, str]]:
    manifest = json.loads((source / "vcpkg.json").read_text(encoding="utf-8"))
    components = []
    for dependency in manifest.get("dependencies", []):
        name = dependency if isinstance(dependency, str) else dependency["name"]
        components.append(
            {
                "name": name,
                "version": f"vcpkg-baseline:{manifest.get('builtin-baseline', 'unknown')}",
                "license": "NOASSERTION",
            }
        )
    for feature in manifest.get("features", {}).values():
        for dependency in feature.get("dependencies", []):
            name = dependency if isinstance(dependency, str) else dependency["name"]
            if not any(component["name"] == name for component in components):
                components.append(
                    {
                        "name": name,
                        "version": f"vcpkg-baseline:{manifest.get('builtin-baseline', 'unknown')}",
                        "license": "NOASSERTION",
                    }
                )
    return sorted(components, key=lambda component: component["name"])


def spdx_document(source: Path, components: list[dict[str, str]]) -> dict[str, object]:
    document_namespace = (
        "https://torrentcraft.invalid/spdx/"
        + hashlib.sha256(source_commit(source).encode()).hexdigest()
    )
    packages = [
        {
            "SPDXID": f"SPDXRef-{component['name'].replace('-', '')}",
            "name": component["name"],
            "versionInfo": component["version"],
            "licenseConcluded": component["license"],
            "licenseDeclared": component["license"],
            "filesAnalyzed": False,
            "downloadLocation": "NOASSERTION",
        }
        for component in components
    ]
    packages.insert(
        0,
        {
            "SPDXID": "SPDXRef-TorrentCraft",
            "name": "TorrentCraft",
            "versionInfo": "1.0.0",
            "licenseConcluded": "MIT",
            "licenseDeclared": "MIT",
            "filesAnalyzed": False,
            "downloadLocation": "NOASSERTION",
        },
    )
    relationships = [
        {
            "spdxElementId": "SPDXRef-TorrentCraft",
            "relationshipType": "DEPENDS_ON",
            "relatedSpdxElement": package["SPDXID"],
        }
        for package in packages
        if package["SPDXID"] != "SPDXRef-TorrentCraft"
    ]
    created = dt.datetime.fromtimestamp(source_epoch(source), tz=dt.timezone.utc).strftime(
        "%Y-%m-%dT%H:%M:%SZ"
    )
    return {
        "spdxVersion": "SPDX-2.3",
        "dataLicense": "CC0-1.0",
        "SPDXID": "SPDXRef-DOCUMENT",
        "name": "TorrentCraft Release dependency inventory",
        "documentNamespace": document_namespace,
        "creationInfo": {
            "created": created,
            "creators": ["Tool: torrentcraft-release-security-audit"],
        },
        "packages": packages,
        "relationships": relationships,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-dir", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--sbom", type=Path, required=True)
    args = parser.parse_args()

    source = args.source_dir.resolve()
    findings = public_header_findings(source)
    components = manifest_components(source)
    license_file = source / "LICENSE"
    if not license_file.is_file():
        raise RuntimeError(f"missing project license: {license_file}")
    report = {
        "schema": "torrentcraft.release-security/v1",
        "vcpkg_baseline": json.loads((source / "vcpkg.json").read_text(encoding="utf-8")).get("builtin-baseline", "unknown"),
        "commit": source_commit(source),
        "public_header_findings": findings,
        "manifest_dependencies": components,
        "license_file": "LICENSE",
        "license_policy": "Dependency copyright files are embedded in the GUI resource bundle when available.",
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.sbom.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    args.sbom.write_text(
        json.dumps(spdx_document(source, components), indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    if findings:
        for finding in findings:
            print(
                f"forbidden public dependency: {finding['file']}:{finding['line']}: "
                f"{finding['text']}"
            )
        return 1
    print(f"security audit passed for {len(components)} manifest dependencies")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
