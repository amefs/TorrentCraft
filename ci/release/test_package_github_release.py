#!/usr/bin/env python3
"""Tests for the GitHub release asset packager."""

from __future__ import annotations

import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

from package_github_release import PLATFORMS, package_platform  # noqa: E402


VERSION = "1.0.0"


def make_artifact(root: Path, platform_name: str, extension: str) -> None:
    stem = f"TorrentCraft-{VERSION}-{platform_name}"
    compliance_extension = "zip" if extension else "tar.gz"
    files = {
        f"{stem}-cli{extension}": b"cli",
        f"{stem}-gui{extension}": b"gui",
        f"{stem}.SBOM.spdx.json": b"sbom",
        f"{stem}.SECURITY-AUDIT.json": b"security",
        f"{stem}.PROVENANCE.json": b"provenance",
        f"{stem}-Qt-LGPL-compliance.{compliance_extension}": b"compliance",
        "test-platform.xml": b"<testsuites />",
        "phase8-test-summary.json": b"{}",
        f"TorrentCraft-{VERSION}-source.tar.gz": b"source",
        f"TorrentCraft-{VERSION}-source.PROVENANCE.json": b"source provenance",
        f"TorrentCraft-{VERSION}-source.SBOM.spdx.json": b"source sbom",
        f"TorrentCraft-{VERSION}-source.SECURITY-AUDIT.json": b"source security",
        "SHA256SUMS": b"old manifest",
    }
    for name, content in files.items():
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content)


class PackageGitHubReleaseTests(unittest.TestCase):
    def test_linux_package_contains_only_support_material(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "linux"
            output = Path(temporary) / "assets"
            root.mkdir()
            make_artifact(root, "linux-musl-x86_64", "")

            assets = package_platform(root, output, VERSION, PLATFORMS[0], 1)

            self.assertEqual(
                [path.name for path in assets],
                [
                    "TorrentCraft-1.0.0-linux-musl-x86_64-cli",
                    "TorrentCraft-1.0.0-linux-musl-x86_64-gui",
                    "TorrentCraft-1.0.0-linux-musl-x86_64-support.tar.gz",
                ],
            )
            with tarfile.open(assets[-1], "r:gz") as archive:
                names = set(archive.getnames())
                self.assertIn("SHA256SUMS", names)
                self.assertIn(
                    "metadata/TorrentCraft-1.0.0-linux-musl-x86_64.SBOM.spdx.json",
                    names,
                )
                self.assertIn("tests/test-platform.xml", names)
                self.assertIn("source/TorrentCraft-1.0.0-source.tar.gz", names)
                manifest = archive.extractfile("SHA256SUMS")
                assert manifest is not None
                text = manifest.read().decode()
                self.assertIn("TorrentCraft-1.0.0-linux-musl-x86_64-cli", text)
                with tempfile.TemporaryDirectory() as extracted:
                    extracted_root = Path(extracted)
                    archive.extractall(extracted_root)
                    for binary in assets[:2]:
                        shutil.copy2(binary, extracted_root / binary.name)
                    result = subprocess.run(
                        ["sha256sum", "-c", "SHA256SUMS"],
                        cwd=extracted_root,
                        capture_output=True,
                        text=True,
                        check=False,
                    )
                    self.assertEqual(result.returncode, 0, result.stderr)

    def test_windows_package_uses_zip(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "windows"
            output = Path(temporary) / "assets"
            root.mkdir()
            make_artifact(root, "windows-x86_64", ".exe")

            assets = package_platform(root, output, VERSION, PLATFORMS[1], 1)

            self.assertTrue(assets[-1].name.endswith("-support.zip"))
            with zipfile.ZipFile(assets[-1]) as archive:
                self.assertIn("SHA256SUMS", archive.namelist())
                self.assertIn(
                    "compliance/TorrentCraft-1.0.0-windows-x86_64-Qt-LGPL-compliance.zip",
                    archive.namelist(),
                )

    def test_unclassified_files_fail(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "linux"
            output = Path(temporary) / "assets"
            root.mkdir()
            make_artifact(root, "linux-musl-x86_64", "")
            (root / "unexpected.txt").write_text("unexpected", encoding="utf-8")

            with self.assertRaisesRegex(RuntimeError, "unclassified release files"):
                package_platform(root, output, VERSION, PLATFORMS[0], 1)


if __name__ == "__main__":
    unittest.main()
