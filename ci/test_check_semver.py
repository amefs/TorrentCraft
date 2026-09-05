#!/usr/bin/env python3

from __future__ import annotations

import unittest

from ci.check_semver import check_contract, cmake_version, parse_release_tag, parse_semver


class SemVerContractTests(unittest.TestCase):
    def test_accepts_semver_and_project_pre_release_forms(self) -> None:
        self.assertEqual(parse_semver("1.2.3").text, "1.2.3")
        self.assertEqual(parse_semver("1.2.3-alpha.1").text, "1.2.3-alpha.1")
        self.assertEqual(parse_semver("1.2.3+ci.42").text, "1.2.3+ci.42")
        self.assertEqual(parse_release_tag("v1.2.3-beta.2").text, "1.2.3-beta.2")

    def test_rejects_invalid_semver_identifiers(self) -> None:
        for value in ("1.2", "01.2.3", "1.2.3-alpha.01", "1.2.3-"):
            with self.subTest(value=value):
                with self.assertRaises(ValueError):
                    parse_semver(value)

    def test_rejects_unapproved_release_tag_forms(self) -> None:
        for tag in ("1.2.3", "v1.2.3+build.1", "v1.2.3-preview.1"):
            with self.subTest(tag=tag):
                with self.assertRaises(ValueError):
                    parse_release_tag(tag)

    def test_requires_matching_cmake_core(self) -> None:
        source = "project(TorrentUtilsCore VERSION 1.2.3 LANGUAGES CXX)\n"
        self.assertEqual(cmake_version(source).text, "1.2.3")
        check_contract(source, "v1.2.3-rc.1")
        with self.assertRaisesRegex(ValueError, "does not match"):
            check_contract(source, "v1.3.0")


if __name__ == "__main__":
    unittest.main()
