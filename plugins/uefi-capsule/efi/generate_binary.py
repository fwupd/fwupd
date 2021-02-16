#!/usr/bin/python3
#
# Copyright (C) 2021 Javier Martinez Canillas <javierm@redhat.com>
# Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: LGPL-2.1+
#
# pylint: disable=missing-docstring, invalid-name

import subprocess
import sys
import argparse
import tempfile


def _run_objcopy_sbat(args, tfd):
    """ append SBAT metadata """
    FWUPD_SUMMARY = "Firmware update daemon"
    FWUPD_URL = "https://github.com/fwupd/fwupd"

    with open(args.infile, "rb") as ifd:
        tfd.write(ifd.read())

    # not specified
    if not args.sbat_distro_id:
        return

    with tempfile.NamedTemporaryFile() as sfd:

        # spec
        sfd.write(
            "{0},{1},{2},{0},{1},{3}\n".format(
                "sbat",
                args.sbat_version,
                "UEFI shim",
                "https://github.com/rhboot/shim/blob/main/SBAT.md",
            ).encode()
        )

        # fwupd
        sfd.write(
            "{0},{1},{2},{0},{3},{4}\n".format(
                args.project_name,
                args.sbat_generation,
                "Firmware update daemon",
                args.project_version,
                FWUPD_URL,
            ).encode()
        )

        # distro specifics, falling back to the project defaults
        sfd.write(
            "{0}-{1},{2},{3},{4},{5},{6}\n".format(
                args.project_name,
                args.sbat_distro_id,
                args.sbat_distro_generation or args.sbat_generation,
                args.sbat_distro_summary or FWUPD_SUMMARY,
                args.sbat_distro_pkgname,
                args.sbat_distro_version or args.project_version,
                args.sbat_distro_url or FWUPD_URL,
            ).encode()
        )

        # all written
        sfd.seek(0)

        # add a section to the object; use `objdump -s -j .sbat` to verify
        argv = [
            args.objcopy,
            "--add-section",
            ".sbat={}".format(sfd.name),
            tfd.name,
        ]
        subprocess.run(argv, check=True)


def _run_objcopy(args):

    with tempfile.NamedTemporaryFile() as tfd:
        _run_objcopy_sbat(args, tfd)

        argv = [
            args.objcopy,
            "-j",
            ".text",
            "-j",
            ".sbat",
            "-j",
            ".sdata",
            "-j",
            ".dynamic",
            "-j",
            ".rel*",
            tfd.name,
            args.outfile,
        ]

        # aarch64 and arm32 don't have an EFI capable objcopy
        # Use 'binary' instead, and add required symbols manually
        if args.arch in ["aarch64", "arm"]:
            argv.extend(["-O", "binary"])
        else:
            argv.extend(["--target", "efi-app-{}".format(args.arch)])
        try:
            subprocess.run(argv, check=True)
        except FileNotFoundError as e:
            print(str(e))
            sys.exit(1)


def _run_genpeimg(args):

    # this is okay if it does not exist
    argv = [
        "genpeimg",
        "-d",
        "+d",
        "+n",
        "-d",
        "+s",
        args.outfile,
    ]
    try:
        subprocess.run(argv, check=True)
    except FileNotFoundError as _:
        pass


if __name__ == "__main__":

    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--objcopy",
        default="objcopy",
        help="Binary file to use for objcopy",
    )
    parser.add_argument(
        "--arch",
        default="x86_64",
        help="EFI architecture",
    )
    parser.add_argument(
        "--project-name",
        help="SBAT project name",
    )
    parser.add_argument(
        "--project-version",
        help="SBAT project version",
    )
    parser.add_argument(
        "--sbat-version",
        default=1,
        type=int,
        help="SBAT version",
    )
    parser.add_argument(
        "--sbat-generation",
        default=0,
        type=int,
        help="SBAT generation",
    )
    parser.add_argument(
        "--sbat-distro-id",
        default=None,
        help="SBAT distribution ID"
    )
    parser.add_argument(
        "--sbat-distro-generation",
        default=None,
        type=int,
        help="SBAT distribution generation",
    )
    parser.add_argument(
        "--sbat-distro-summary",
        default=None,
        help="SBAT distribution summary",
    )
    parser.add_argument(
        "--sbat-distro-pkgname",
        default=None,
        help="SBAT distribution package name",
    )
    parser.add_argument(
        "--sbat-distro-version",
        default=None,
        help="SBAT distribution version",
    )
    parser.add_argument(
        "--sbat-distro-url",
        default=None,
        help="SBAT distribution URL",
    )
    parser.add_argument(
        "infile",
        help="Input file",
    )
    parser.add_argument(
        "outfile",
        help="Output file",
    )
    _args = parser.parse_args()
    _run_objcopy(_args)
    _run_genpeimg(_args)

    sys.exit(0)
