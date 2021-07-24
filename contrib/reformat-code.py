#!/usr/bin/python3
#
# Copyright (C) 2017 Dell Inc.
#
# SPDX-License-Identifier: LGPL-2.1+
#

import os
import sys
import subprocess

CLANG_DIFF_FORMATTERS = [
    "clang-format-diff-11",
    "clang-format-diff-13",
    "clang-format-diff",
]


def select_clang_version(formatters):
    for formatter in formatters:
        try:
            ret = subprocess.check_call(
                [formatter, "--help"], stdout=subprocess.PIPE, stderr=subprocess.PIPE
            )
            if ret == 0:
                return formatter
        except FileNotFoundError:
            continue
    print("No clang formatter installed")
    sys.exit(1)


## Entry Point ##
if __name__ == "__main__":
    base = os.getenv("GITHUB_BASE_REF")
    if base:
        base = "origin/%s" % base
    else:
        base = "HEAD"
    formatter = select_clang_version(CLANG_DIFF_FORMATTERS)
    ret = subprocess.run(
        ["git", "diff", "-U0", base], capture_output=True, check=True, text=True
    )
    if ret.returncode:
        print("Failed to run git diff: %s" % ret.stderr)
        sys.exit(1)
    ret = subprocess.run(
        [formatter, "-p1"], input=ret.stdout, capture_output=True, text=True
    )
    if ret.returncode:
        print("Failed to run formatter: %s % ret.stderr")
        sys.exit(1)
    ret = subprocess.run(
        ["patch", "-p0"], input=ret.stdout, capture_output=True, text=True
    )
    if ret.returncode:
        print("Failed to run patch: %s" % ret.stderr)
        sys.exit(1)
    sys.exit(0)
