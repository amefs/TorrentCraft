#!/usr/bin/env python3

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from summarize_junit import summarize_reports


class JUnitSummaryTests(unittest.TestCase):
    def write_report(self, directory: Path, name: str, content: str) -> Path:
        path = directory / name
        path.write_text(content, encoding="utf-8")
        return path

    def test_sums_testcases_and_uses_suite_assertions(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = self.write_report(
                Path(temporary),
                "report.xml",
                """
                <testsuites>
                  <testsuite name="core" tests="2" assertions="7">
                    <testcase name="passed" />
                    <testcase name="skipped"><skipped message="new skip reason" /></testcase>
                  </testsuite>
                </testsuites>
                """,
            )

            summary = summarize_reports([path], "commit")

        self.assertEqual(summary["test_cases"], 2)
        self.assertEqual(summary["passed"], 1)
        self.assertEqual(summary["skipped"], 1)
        self.assertEqual(summary["expected_skipped"], 0)
        self.assertEqual(summary["unexpected_skipped"], 1)
        self.assertEqual(summary["assertions"], 7)
        self.assertEqual(summary["status"], "failed")

    def test_known_environment_skip_is_audited_but_passes(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = self.write_report(
                Path(temporary),
                "report.xml",
                """
                <testsuite>
                  <testcase classname="torrent" name="symlink">
                    <skipped message="filesystem symlink creation is unavailable: Operation not permitted" />
                  </testcase>
                  <testcase classname="cli" name="unicode">
                    <skipped>the filesystem cannot round-trip the Unicode content name</skipped>
                  </testcase>
                </testsuite>
                """,
            )

            summary = summarize_reports([path], "commit")

        self.assertEqual(summary["skipped"], 2)
        self.assertEqual(summary["expected_skipped"], 2)
        self.assertEqual(summary["unexpected_skipped"], 0)
        self.assertEqual(summary["status"], "passed")
        details = summary["reports"][0]["skip_details"]
        self.assertEqual(details[0]["category"], "symlink_creation_unavailable")
        self.assertEqual(details[1]["category"], "unicode_filename_unavailable")

    def test_ctest_skip_marker_is_expected_only_for_registered_capability_tests(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = self.write_report(
                Path(temporary),
                "report.xml",
                """
                <testsuite>
                  <testcase classname="TorrentUtils.Core" name="given_unreadable_regular_file_when_verified_then_access_denied_is_a_result_error">
                    <skipped message="SKIP_RETURN_CODE=4" />
                  </testcase>
                  <testcase classname="TorrentUtils.Core" name="new_capability_test">
                    <skipped message="SKIP_RETURN_CODE=4" />
                  </testcase>
                </testsuite>
                """,
            )

            summary = summarize_reports([path], "commit")

        self.assertEqual(summary["expected_skipped"], 1)
        self.assertEqual(summary["unexpected_skipped"], 1)
        self.assertEqual(summary["status"], "failed")
        details = summary["reports"][0]["skip_details"]
        self.assertEqual(details[0]["category"], "unreadable_file_permissions_unenforced")
        self.assertEqual(details[1]["category"], "unexpected")

    def test_counts_failure_and_error_cases_across_reports(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            first = self.write_report(
                directory,
                "first.xml",
                '<testsuite><testcase name="failure"><failure /></testcase></testsuite>',
            )
            second = self.write_report(
                directory,
                "second.xml",
                '<testsuite><testcase name="error"><error /></testcase></testsuite>',
            )

            summary = summarize_reports([first, second], "commit")

        self.assertEqual(summary["test_cases"], 2)
        self.assertEqual(summary["passed"], 0)
        self.assertEqual(summary["failures"], 1)
        self.assertEqual(summary["errors"], 1)
        self.assertEqual(summary["status"], "failed")


if __name__ == "__main__":
    unittest.main()
