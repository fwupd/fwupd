#!/usr/bin/python3
# pylint: disable=missing-docstring
#
# SPDX-License-Identifier: LGPL-2.1+

import glob
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

    with open(sbat_csv, "a+", encoding="utf-8") as sfd:
        sfd.write(
            "{0},{1},{2},{0},{1},{3}\n".format(
                "sbat",
                sbat_version,
                "UEFI shim",
                "https://github.com/rhboot/shim/blob/main/SBAT.md",
            )
        )
        sfd.write(
            "{0},{1},{2},{0},{3},{4}\n".format(
                project_name,
                sbat_component_generation,
                "Firmware update daemon",
                project_version,
                "https://github.com/fwupd/fwupd",
            )
        )

    distro_csv = glob.glob(glob_csv)
    if len(distro_csv) > 1:
        print("More than one CSV for SBAT metadata is present")
        sys.exit(1)

    if distro_csv:
        with open(distro_csv[0], "r", encoding="utf-8") as cfd, open(
            sbat_csv, "a+", encoding="utf-8"
        ) as sfd:
            data = cfd.read()
            sfd.write(data)

    cmd = [
        objcopy_cmd,
        "--add-section",
        ".sbat=%s" % sbat_csv,
        outfile,
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


if len(sys.argv) != 11:
    print("Not enough arguments")
    sys.exit(1)

infile = sys.argv[1]
outfile = sys.argv[2]
target = sys.argv[3]
efi_objcopy = sys.argv[4]
glob_csv = sys.argv[5]
sbat_csv = sys.argv[6]
sbat_version = sys.argv[7]
project_name = sys.argv[8]
project_version = sys.argv[9]
sbat_component_generation = sys.argv[10]

run_objcopy()
run_genpeimg()

sys.exit(0)
