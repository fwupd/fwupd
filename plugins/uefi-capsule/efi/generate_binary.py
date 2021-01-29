#!/usr/bin/python3
# pylint: disable=missing-docstring
#
# SPDX-License-Identifier: LGPL-2.1+

import subprocess
from subprocess import PIPE
import sys


def run_objcopy():
    objcopy_cmd = subprocess.run(
        ["sh", "-c", "command -v %s" % efi_objcopy],
        stdout=PIPE,
        stderr=PIPE,
        encoding="utf-8",
        check=True,
    ).stdout.strip()
    if not objcopy_cmd:
        print("The %s command was not found" % efi_objcopy)
        sys.exit(1)

    cmd = [
        objcopy_cmd,
        "-j",
        ".text",
        "-j",
        ".sdata",
        "-j",
        ".dynamic",
        "-j",
        ".rel*",
        infile,
        outfile,
        target,
    ]
    subprocess.run(cmd, check=True)


def run_genpeimg():
    genpeimg_cmd = subprocess.run(
        ["sh", "-c", "command -v genpeimg"],
        stdout=PIPE,
        stderr=PIPE,
        encoding="utf-8",
        check=False,
    ).stdout.strip()
    if not genpeimg_cmd:
        return

    cmd = [
        genpeimg_cmd,
        "-d",
        "+d",
        "+n",
        "-d",
        "+s",
        outfile,
    ]
    subprocess.run(cmd, check=True)


if len(sys.argv) != 5:
    print("Not enough arguments")
    sys.exit(1)

infile = sys.argv[1]
outfile = sys.argv[2]
target = sys.argv[3]
efi_objcopy = sys.argv[4]

run_objcopy()
run_genpeimg()

sys.exit(0)
