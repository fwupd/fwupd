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
    "clang-format-13",
    "clang-format",
]
FIXUPS = {"g_autoptr (": "g_autoptr(", "sizeof (": "sizeof(", "g_auto (": "g_auto("}


def parse_args():
    parser = argparse.ArgumentParser(description="Reformat code to match style")
    parser.add_argument("files", nargs="*", help="files to reformat")
    args = parser.parse_args()
    if len(args.files) == 0:
        parser.print_help()
        sys.exit(0)
    return args


def select_clang_version():
    for formatter in CLANG_FORMATTERS:
        try:
            ret = subprocess.check_call([formatter, "--version"])
            if ret == 0:
                return formatter
        except FileNotFoundError:
            continue
    return None


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


## Entry Point ##
if __name__ == "__main__":
    args = parse_args()
    formatter = select_clang_version()
    if not formatter:
        print("No clang formatter installed")
        sys.exit(1)
    for f in args.files:
        if not os.path.exists(f):
            print("%s does not exist" % f)
            sys.exit(1)
        if not (f.endswith(".c") or f.endswith(".h")):
            print("Skipping %s" % f)
            continue
        reformat_file(formatter, f)
    sys.exit(0)
