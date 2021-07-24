#!/usr/bin/python3
#
# Copyright (C) 2017 Dell Inc.
#
# SPDX-License-Identifier: LGPL-2.1+
#

import argparse
import os
import sys
import subprocess

CLANG_FORMATTERS = [
    "clang-format-11",
    "clang-format-13",
    "clang-format",
]
CLANG_DIFF_FORMATTERS = [
    "clang-format-diff-11",
    "clang-format-diff-13",
    "clang-format-diff",
]
FIXUPS = {}


def parse_args():
    parser = argparse.ArgumentParser(description="Reformat code to match style")
    parser.add_argument("files", nargs="*", help="files to reformat")
    args = parser.parse_args()
    return args


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


def reformat_file(formatter, f):
    print("Reformatting %s using %s" % (f, formatter))
    lines = None
    with open(f, "r") as rfd:
        lines = rfd.readlines()
    ret = subprocess.run(
        [formatter, "-style=file", f], capture_output=True, check=True, text=True
    )
    if ret.returncode:
        print("Failed to run formatter")
        sys.exit(1)
    formatted = ret.stdout.splitlines(True)
    save = False
    for idx, line in enumerate(formatted):
        for fixup in FIXUPS:
            if fixup in line:
                formatted[idx] = line.replace(fixup, FIXUPS[fixup])
        if not save and formatted[idx] != lines[idx]:
            save = True
    if save:
        with open(f, "w") as wfd:
            wfd.writelines(formatted)


def reformat_files(files):
    formatter = select_clang_version(CLANG_FORMATTERS)
    for f in files:
        if not os.path.exists(f):
            print("%s does not exist" % f)
            sys.exit(1)
        if not (f.endswith(".c") or f.endswith(".h")):
            print("Skipping %s" % f)
            continue
        reformat_file(formatter, f)


def reformat_diff():
    formatter = select_clang_version(CLANG_DIFF_FORMATTERS)
    ret = subprocess.run(
        ["git", "diff", "-U0", "HEAD"], capture_output=True, check=True, text=True
    )
    if ret.returncode:
        print("Failed to run git diff")
        sys.exit(1)
    ret = subprocess.run(
        [formatter, "-p1"], input=ret.stdout, capture_output=True, check=True, text=True
    )
    if ret.returncode:
        print("Failed to run formatter")
        sys.exit(1)
    formatted = ret.stdout.splitlines(True)
    for idx, line in enumerate(formatted):
        if not line.startswith("+"):
            continue
        for fixup in FIXUPS:
            if fixup in line:
                formatted[idx] = line.replace(fixup, FIXUPS[fixup])
    fixedup = "".join(formatted)
    ret = subprocess.run(
        ["patch", "-p0"], input=fixedup, capture_output=True, check=True, text=True
    )
    if ret.returncode:
        print("Failed to run patch")
        sys.exit(1)


## Entry Point ##
if __name__ == "__main__":
    args = parse_args()
    if len(args.files) == 0:
        reformat_diff()
    else:
        reformat_files(args.files)
    sys.exit(0)
