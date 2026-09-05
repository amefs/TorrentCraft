#!/usr/bin/env python3
"""Validate TorrentCraft's CMake version and Git release-tag contract."""

from __future__ import annotations

import argparse
import os
import re
import sys
from dataclasses import dataclass
from pathlib import Path


_PROJECT_VERSION_PATTERN = re.compile(
    r"project\s*\(\s*TorrentUtilsCore\s+VERSION\s+"
    r"(?P<version>[^\s\)]+)",
    re.IGNORECASE,
)
_SEMVER_PATTERN = re.compile(
    r"^(?P<major>0|[1-9][0-9]*)\."
    r"(?P<minor>0|[1-9][0-9]*)\."
    r"(?P<patch>0|[1-9][0-9]*)"
    r"(?:-(?P<prerelease>[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?"
    r"(?:\+(?P<build>[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?$"
)
_PROJECT_PRERELEASE_PATTERN = re.compile(
    r"^(?:alpha|beta|rc)\.(?:0|[1-9][0-9]*)$"
)


@dataclass(frozen=True)
class Version:
    """A parsed SemVer value."""

    major: int
    minor: int
    patch: int
    prerelease: str | None = None
    build: str | None = None

    @property
    def core(self) -> tuple[int, int, int]:
        return self.major, self.minor, self.patch

    @property
    def core_text(self) -> str:
        return f"{self.major}.{self.minor}.{self.patch}"

    @property
    def text(self) -> str:
        value = self.core_text
        if self.prerelease:
            value += f"-{self.prerelease}"
        if self.build:
            value += f"+{self.build}"
        return value


def parse_semver(value: str) -> Version:
    """Parse a complete SemVer 2.0.0 value."""

    match = _SEMVER_PATTERN.fullmatch(value)
    if not match:
        raise ValueError(f"invalid SemVer value: {value!r}")

    prerelease = match.group("prerelease")
    if prerelease:
        for identifier in prerelease.split("."):
            if (
                identifier.isdigit()
                and len(identifier) > 1
                and identifier.startswith("0")
            ):
                raise ValueError(
                    "numeric pre-release identifier has a leading zero: "
                    f"{value!r}"
                )

    return Version(
        int(match.group("major")),
        int(match.group("minor")),
        int(match.group("patch")),
        prerelease,
        match.group("build"),
    )


def parse_release_tag(tag: str) -> Version:
    """Parse the project's v-prefixed stable or approved pre-release tag."""

    if not tag.startswith("v"):
        raise ValueError("release tag must use the v prefix")
    version = parse_semver(tag[1:])
    if version.build:
        raise ValueError("release tags must not contain build metadata")
    if version.prerelease and not _PROJECT_PRERELEASE_PATTERN.fullmatch(
        version.prerelease
    ):
        raise ValueError("release pre-release must be alpha.N, beta.N, or rc.N")
    return version


def cmake_version(cmake_text: str) -> Version:
    """Extract and validate the stable project version from CMake."""

    match = _PROJECT_VERSION_PATTERN.search(cmake_text)
    if not match:
        raise ValueError("could not find TorrentUtilsCore project version")
    version = parse_semver(match.group("version"))
    if version.prerelease or version.build:
        raise ValueError("CMake project version must be a stable X.Y.Z value")
    return version


def check_contract(
    cmake_text: str, tag: str | None = None
) -> tuple[Version, Version | None]:
    """Validate CMake and, when supplied, the release tag relationship."""

    source = cmake_version(cmake_text)
    if not tag:
        return source, None

    release = parse_release_tag(tag)
    if release.core != source.core:
        raise ValueError(
            "release tag version does not match CMake version: "
            f"tag={release.core_text} source={source.core_text}"
        )
    return source, release


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--cmake-file",
        type=Path,
        default=Path("CMakeLists.txt"),
        help="path to the project's CMakeLists.txt",
    )
    parser.add_argument(
        "--tag",
        default=None,
        help="release tag to validate; defaults to CI_COMMIT_TAG",
    )
    args = parser.parse_args()
    tag = args.tag if args.tag is not None else os.environ.get("CI_COMMIT_TAG") or None

    try:
        source, release = check_contract(
            args.cmake_file.read_text(encoding="utf-8"), tag
        )
    except (OSError, ValueError) as error:
        print(f"semantic version contract failed: {error}", file=sys.stderr)
        return 1

    print(f"CMake version: {source.text}")
    if release:
        print(f"release tag: v{release.text}")
    else:
        print("release tag: none (branch pipeline)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
