#!/usr/bin/python3
# -*- coding: utf-8 -*-
#
# Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: LGPL-2.1+

import struct
import zlib
import argparse


def main(bin_fn, dfu_fn, pad, vid, pid, rev):

    # read binary file
    with open(bin_fn, "rb") as f:
        blob = f.read()

    # pad blob to a specific size
    if pad:
        while len(blob) < int(pad, 16):
            blob += b"\0"

    # create DFU footer with checksum
    blob += struct.pack(
        "<HHHH3sB",
        int(rev, 16),  # version
        int(pid, 16),  # PID
        int(vid, 16),  # VID
        0x0100,  # DFU version
        b"UFD",  # signature
        0x10,
    )  # hdrlen
    crc32 = zlib.crc32(blob) ^ 0xFFFFFFFF
    blob += struct.pack("<L", crc32)

    # write binary file
    with open(dfu_fn, "wb") as f:
        f.write(blob)


if __name__ == "__main__":

    # parse args
    parser = argparse.ArgumentParser(description="Add DFU footer on firmware")
    parser.add_argument("--bin", help="Path to the .bin file", required=True)
    parser.add_argument("--dfu", help="Output DFU file path", required=True)
    parser.add_argument(
        "--pad", help="Pad to a specific size, e.g. 0x4000", default=None
    )
    parser.add_argument("--vid", help="Vendor ID, e.g. 0x273f", required=True)
    parser.add_argument("--pid", help="Product ID, e.g. 0x1002", required=True)
    parser.add_argument("--rev", help="Revision, e.g. 0x1000", required=True)
    args = parser.parse_args()
    main(args.bin, args.dfu, args.pad, args.vid, args.pid, args.rev)
