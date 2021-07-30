#!/usr/bin/python3
#
# Copyright (C) 2017 Dell Inc.
#
# SPDX-License-Identifier: LGPL-2.1+
#

import os
import sys
import subprocess
import argparse

CLANG_DIFF_FORMATTERS = [
    "clang-format-diff-11",
    "clang-format-diff-13",
    "clang-format-diff",
]


def parse_args():
    parser = argparse.ArgumentParser(
        description="Reformat C code to match project style",
        epilog="Call with no argument to reformat uncommitted code.",
    )
    parser.add_argument(
        "commit", nargs="*", default="", help="Reformat all changes since this commit"
    )
    parser.add_argument(
        "--debug", action="store_true", help="Display all launched commands"
    )
    return parser.parse_args()


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
    args = parse_args()
    base = os.getenv("GITHUB_BASE_REF")
    if base:
        base = "origin/%s" % base
    else:
        if args.commit:
            base = args.commit[0]
        else:
            base = "HEAD"
    cmd = ["git", "describe", base]
    if args.debug:
        print(cmd)
    ret = subprocess.run(cmd, capture_output=True)
    if ret.returncode:
        if args.debug:
            print(ret.stderr)
        base = "HEAD"
    print("Reformatting code against %s" % base)
    formatter = select_clang_version(CLANG_DIFF_FORMATTERS)
    cmd = ["git", "diff", "-U0", base]
    if args.debug:
        print(cmd)
    ret = subprocess.run(cmd, capture_output=True, text=True)
    if ret.returncode:
        print("Failed to run %s\n%s" % (cmd, ret.stderr.strip()))
        sys.exit(1)
    cmd = [formatter, "-p1"]
    if args.debug:
        print(cmd)
    ret = subprocess.run(cmd, input=ret.stdout, capture_output=True, text=True)
    if ret.returncode:
        print("Failed to run %s\n%s" % (cmd, ret.stderr.strip()))
        sys.exit(1)
    cmd = ["patch", "-p0"]
    if args.debug:
        print(cmd)
    ret = subprocess.run(cmd, input=ret.stdout, capture_output=True, text=True)
    if ret.returncode:
        print("Failed to run %s\n%s" % (cmd, ret.stderr.strip()))
        sys.exit(1)
    sys.exit(0)
