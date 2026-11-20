#!/usr/bin/env python3
"""Run clang-tidy over the project sources recorded in a CMake compile database.

The file list comes from compile_commands.json, so new sources are checked without editing CI.
Every finding is an error (--warnings-as-errors='*'); the script exits non-zero if any file has
findings or clang-tidy fails.

Usage:
  ci/run-clang-tidy.py BUILD_DIR [--include REGEX] [--exclude REGEX] [--extra FILE ...]
                       [--clang-tidy BINARY] [--jobs N] [-- EXTRA_CLANG_TIDY_ARGS ...]
"""

from __future__ import annotations

import argparse
import concurrent.futures
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path


def default_clang_tidy() -> str:
    for name in ("clang-tidy-18", "clang-tidy"):
        if shutil.which(name):
            return name
    return "clang-tidy-18"


def project_files(build_dir: Path, root: Path, include: re.Pattern[str],
                  exclude: re.Pattern[str] | None) -> list[str]:
    database = json.loads((build_dir / "compile_commands.json").read_text(encoding="utf-8"))
    selected: set[str] = set()
    for entry in database:
        path = Path(entry["file"])
        if not path.is_absolute():
            path = Path(entry["directory"]) / path
        try:
            relative = path.resolve().relative_to(root).as_posix()
        except ValueError:
            continue  # vcpkg or system sources outside the project
        if relative.startswith("out/") or not include.search(relative):
            continue
        if exclude is not None and exclude.search(relative):
            continue
        selected.add(relative)
    return sorted(selected)


def main() -> int:
    argv = sys.argv[1:]
    passthrough: list[str] = []
    if "--" in argv:
        split = argv.index("--")
        argv, passthrough = argv[:split], argv[split + 1:]

    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("build_dir", type=Path)
    parser.add_argument("--include", default=r"^(src|tests)/",
                        help="regex a project-relative path must match")
    parser.add_argument("--exclude", default=None, help="regex of project-relative paths to skip")
    parser.add_argument("--extra", nargs="*", default=[],
                        help="files to check that are not in the compile database")
    parser.add_argument("--clang-tidy", default=default_clang_tidy())
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 1)
    args = parser.parse_args(argv)

    root = Path(__file__).resolve().parent.parent
    build_dir = args.build_dir if args.build_dir.is_absolute() else root / args.build_dir
    exclude = re.compile(args.exclude) if args.exclude else None
    files = project_files(build_dir, root, re.compile(args.include), exclude)
    files += [extra for extra in args.extra if extra not in files]
    if not files:
        print("run-clang-tidy: no files selected", file=sys.stderr)
        return 2

    command = [args.clang_tidy, "-p", str(build_dir), "--quiet", "--warnings-as-errors=*",
               *passthrough]

    def check(path: str) -> tuple[str, int, str]:
        result = subprocess.run([*command, path], cwd=root, capture_output=True, text=True,
                                check=False)
        return path, result.returncode, result.stdout + result.stderr

    print(f"run-clang-tidy: checking {len(files)} files with {args.clang_tidy}", flush=True)
    failed: list[str] = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        for path, code, output in pool.map(check, files):
            findings = "\n".join(line for line in output.splitlines()
                                 if not re.match(r"^\d+ warnings? (and \d+ errors? )?generated\.$",
                                                 line))
            if code != 0:
                failed.append(path)
                print(f"--- {path} (exit {code})\n{findings}", flush=True)
            elif findings.strip():
                print(f"--- {path}\n{findings}", flush=True)
    if failed:
        print(f"run-clang-tidy: {len(failed)} of {len(files)} files failed: {' '.join(failed)}",
              file=sys.stderr)
        return 1
    print(f"run-clang-tidy: {len(files)} files clean")
    return 0


if __name__ == "__main__":
    sys.exit(main())
