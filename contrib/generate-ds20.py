#!/usr/bin/python3
# pylint: disable=invalid-name,missing-docstring
#
# Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: LGPL-2.1+
#
# pylint: disable=consider-using-f-string

import sys
import argparse
import configparser
import base64
from typing import List


if __name__ == "__main__":

    parser = argparse.ArgumentParser()
    parser.add_argument(
        "-b",
        "--bufsz",
        type=int,
        help="Buffer size in bytes",
    )
    parser.add_argument("-i", "--instance-id", type=str, help="Device instance ID")
    parser.add_argument("filename", action="store", type=str, help="Quirk filename")

    args = parser.parse_args()
    if not args.bufsz:
        parser.print_help()
        sys.exit(1)

    config = configparser.ConfigParser()
    config.optionxform = str
    try:
        config.read(args.filename)
    except configparser.MissingSectionHeaderError:
        print("Not a quirk file")
        sys.exit(1)

    # fall back to the default if there is only one device in the quirk file
    if not args.instance_id:
        sections = config.sections()
        if len(sections) != 1:
            print("Multiple devices found, use --instance-id to choose between:")
            for section in sections:
                print(" • {}".format(section))
            sys.exit(1)
        args.instance_id = sections[0]

    # create the smallest kv store possible
    lines: List[str] = []
    try:
        for key in config[args.instance_id]:
            if key in ["Inhibit", "Issue"]:
                print("WARNING: skipping key {}".format(key))
                continue
            value = config[args.instance_id][key]
            lines.append("{}={}".format(key, value))
    except KeyError:
        print("No {} section".format(args.instance_id))
        sys.exit(1)

    # pad to the buffer size
    buf: bytes = "\n".join(lines).encode()
    if len(buf) > args.bufsz:
        print("Quirk data is larger than bufsz")
        sys.exit(1)
    buf = buf.ljust(args.bufsz, b"\0")

    # success
    print("DS20 descriptor control transfer data:")
    print(" ".join(["{:02x}".format(val) for val in list(buf)]))
    print(base64.b64encode(buf).decode())
