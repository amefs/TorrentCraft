#!/usr/bin/env python3
"""Aggregate JUnit reports for a release."""

from __future__ import annotations

import argparse
import json
import os
import sys
import xml.etree.ElementTree as ET
from pathlib import Path
from typing import Iterable


SCHEMA = "torrentcraft.release-test-summary/v1"


# These tests probe capabilities that are not guaranteed by every supported
# CI runner. Keep the prefixes narrow so a new or changed skip reason must be
# reviewed instead of silently becoming release-eligible.
EXPECTED_SKIP_REASON_PREFIXES = (
    (
        "unreadable_file_permissions_unenforced",
        "the test filesystem does not enforce unreadable file permissions",
    ),
    (
        "unwritable_directory_permissions_unenforced",
        "the test filesystem does not enforce unwritable directory permissions",
    ),
    ("symlink_creation_unavailable", "filesystem symlink creation is unavailable:"),
    (
        "directory_symlink_creation_unavailable",
        "filesystem directory symlink creation is unavailable:",
    ),
    (
        "cyclic_symlink_creation_unavailable",
        "filesystem cyclic symlink creation is unavailable:",
    ),
    (
        "unicode_filename_unavailable",
        "the filesystem cannot round-trip the Unicode content name",
    ),
    (
        "unicode_filename_libtorrent_unavailable",
        "libtorrent cannot open the Unicode content name on this environment even though ",
    ),
)
# CTest's --output-junit conversion replaces Catch2's skip message with this
# marker. Some CTest versions omit the message attribute entirely. These exact
# test names are the capability probes that can return either representation;
# an unregistered test name remains an unexpected skip.
CTEST_SKIP_RETURN_CODE_MESSAGE = "SKIP_RETURN_CODE=4"
CTEST_EMPTY_SKIP_MESSAGE = ""
EXPECTED_CTEST_SKIP_TEST_CATEGORIES = (
    (
        "given_unreadable_create_payload_when_hashed_then_access_denied_leaves_no_output",
        "unreadable_file_permissions_unenforced",
    ),
    (
        "given_unreadable_regular_file_when_verified_then_access_denied_is_a_result_error",
        "unreadable_file_permissions_unenforced",
    ),
    (
        "given_unwritable_target_directory_when_created_then_write_failure_leaves_no_output",
        "unwritable_directory_permissions_unenforced",
    ),
    (
        "given_unicode_content_path_when_created_then_torrent_is_written",
        "unicode_content_path_capability_unavailable",
    ),
    (
        "given_mixed_unicode_content_path_when_created_then_human_output_preserves_target",
        "unicode_content_path_capability_unavailable",
    ),
)


def local_name(tag: str) -> str:
    """Return an XML tag name without an optional namespace."""

    return tag.rsplit("}", 1)[-1]


def integer_attribute(element: ET.Element, name: str) -> int | None:
    value = element.attrib.get(name)
    if value is None:
        return None
    try:
        return int(value)
    except ValueError:
        return None


def elements(root: ET.Element, name: str) -> Iterable[ET.Element]:
    return (element for element in root.iter() if local_name(element.tag) == name)


def normalized_text(value: str) -> str:
    return " ".join(value.split())


def skipped_message(testcase: ET.Element) -> str:
    for child in testcase:
        if local_name(child.tag) != "skipped":
            continue
        message = child.attrib.get("message")
        if message:
            return normalized_text(message)
        return normalized_text("".join(child.itertext()))
    return ""


def classify_skip_reason(message: str) -> str | None:
    normalized = normalized_text(message)
    for category, prefix in EXPECTED_SKIP_REASON_PREFIXES:
        if normalized.startswith(prefix):
            return category
    return None


def classify_skip(testcase: ET.Element) -> str | None:
    message = skipped_message(testcase)
    category = classify_skip_reason(message)
    if category is not None:
        return category
    if message not in (CTEST_SKIP_RETURN_CODE_MESSAGE, CTEST_EMPTY_SKIP_MESSAGE):
        return None
    name = normalized_text(testcase.attrib.get("name", ""))
    for test_name, expected_category in EXPECTED_CTEST_SKIP_TEST_CATEGORIES:
        if name == test_name or name.endswith("." + test_name):
            return expected_category
    return None


def summarize_report(path: Path) -> dict[str, object]:
    if not path.is_file():
        raise RuntimeError(f"missing JUnit report: {path}")
    try:
        root = ET.parse(path).getroot()
    except ET.ParseError as error:
        raise RuntimeError(f"invalid JUnit report {path}: {error}") from error

    testcases = list(elements(root, "testcase"))
    skipped = 0
    expected_skipped = 0
    unexpected_skipped = 0
    failures = 0
    errors = 0
    testcase_assertions: list[int] = []
    skip_details: list[dict[str, str]] = []
    for testcase in testcases:
        children = {local_name(child.tag) for child in testcase}
        if "skipped" in children:
            skipped += 1
            message = skipped_message(testcase)
            category = classify_skip(testcase)
            if category is None:
                unexpected_skipped += 1
            else:
                expected_skipped += 1
            skip_details.append(
                {
                    "name": testcase.attrib.get("name", ""),
                    "classname": testcase.attrib.get("classname", ""),
                    "message": message,
                    "category": category or "unexpected",
                }
            )
        failures += int("failure" in children)
        errors += int("error" in children)
        assertions = integer_attribute(testcase, "assertions")
        if assertions is not None:
            testcase_assertions.append(assertions)

    suite_assertions = [
        assertions
        for suite in elements(root, "testsuite")
        if (assertions := integer_attribute(suite, "assertions")) is not None
    ]
    assertions = sum(testcase_assertions or suite_assertions)
    test_cases = len(testcases)
    passed = max(0, test_cases - skipped - failures - errors)
    return {
        "path": str(path),
        "test_cases": test_cases,
        "passed": passed,
        "skipped": skipped,
        "expected_skipped": expected_skipped,
        "unexpected_skipped": unexpected_skipped,
        "failures": failures,
        "errors": errors,
        "assertions": assertions,
        "skip_details": skip_details,
    }


def summarize_reports(paths: list[Path], source_commit: str) -> dict[str, object]:
    reports = [summarize_report(path) for path in paths]
    totals = {
        field: sum(int(report[field]) for report in reports)
        for field in (
            "test_cases",
            "passed",
            "skipped",
            "expected_skipped",
            "unexpected_skipped",
            "failures",
            "errors",
            "assertions",
        )
    }
    status = (
        "passed"
        if not any(
            totals[field] for field in ("unexpected_skipped", "failures", "errors")
        )
        else "failed"
    )
    return {
        "schema": SCHEMA,
        "source_commit": source_commit,
        "status": status,
        **totals,
        "reports": reports,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--source-commit",
        default=os.environ.get("CI_COMMIT_SHA", "unknown"),
    )
    parser.add_argument("reports", type=Path, nargs="+")
    args = parser.parse_args()

    try:
        summary = summarize_reports(args.reports, args.source_commit)
    except RuntimeError as error:
        print(f"JUnit summary failed: {error}", file=sys.stderr)
        return 1

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(
        "JUnit summary: "
        f"{summary['test_cases']} test cases, "
        f"{summary['failures']} failures, "
        f"{summary['errors']} errors, "
        f"{summary['skipped']} skipped "
        f"({summary['expected_skipped']} expected, "
        f"{summary['unexpected_skipped']} unexpected)"
    )
    return 0 if summary["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
