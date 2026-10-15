#!/usr/bin/env python3
#
# Copyright 2026 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Generate a Cobertura coverage report using gcovr. The behavior can be changed
# using these environment variables:
#
#   GCOV                  the gcov executable, e.g. "llvm-cov gcov"
#   GCOVR_IGNORE_ERRORS   passed to --gcov-ignore-errors
#   GCOVR_OUTPUT          where to write the report (default: coverage.xml)
#   GCOVR_SEARCH          the directory to search for coverage data

import os
import shlex
import subprocess
import sys

FILTERS = [
    "(^|/)libfwupd/",
    "(^|/)libfwupdplugin/",
    "(^|/)plugins/",
    "(^|/)src/",
]
EXCLUDES = [
    "(^|/)subprojects/",
    ".*/fwupd-context-test\\.c",
    ".*/fwupd-thread-test\\.c",
]
EXCLUDE_LINES = [
    "G_OBJECT_WARN_INVALID",
    "G_DEFINE_TYPE",
    "JSON_NODE_HOLDS_OBJECT",
    "g_autoptr",
    "g_critical",
    "g_warning",
    "g_assert_cmpfloat_with_epsilon",
    "g_assert_cmpint",
    "g_assert_cmpstr",
    "g_assert_cmpuint",
    "g_assert_error",
    "g_assert_false",
    "g_assert_no_error",
    "g_assert_nonnull",
    "g_assert_not_reached",
    "g_assert_null",
    "g_assert_true",
    "g_return_if_fail",
    "g_return_val_if_fail",
]


def main() -> int:
    # if invoked outside of CI
    if os.environ.get("CI") != "true":
        print("Not running in CI")
        return 1

    # set GCOVR_OUTPUT to redirect the report back to the workspace root
    output = os.environ.get("GCOVR_OUTPUT") or "coverage.xml"

    argv = ["gcovr", "-x"]
    if gcov := os.environ.get("GCOV"):
        argv += ["--gcov-executable", gcov]
    if ignore_errors := os.environ.get("GCOVR_IGNORE_ERRORS"):
        argv += ["--gcov-ignore-errors", ignore_errors]
    for pattern in FILTERS:
        argv += ["--filter", pattern]
    for pattern in EXCLUDES:
        argv += ["--exclude", pattern]
    argv += ["--exclude-lines-by-pattern", f"^.*({'|'.join(EXCLUDE_LINES)}).*$"]
    argv += ["-o", output]
    if search := os.environ.get("GCOVR_SEARCH"):
        argv.append(search)
    print(f"+ {shlex.join(argv)}", flush=True)
    subprocess.run(argv, check=True)

    # make the paths relative to the source root
    with open(output, encoding="utf-8") as f:
        report = f.read().replace("build/", "")
    with open(output, "w", encoding="utf-8") as f:
        f.write(report)

    # fail if no source files matched
    if "<class " not in report:
        print(
            "coverage.py: no coverage data was captured -- check the gcovr filters",
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
