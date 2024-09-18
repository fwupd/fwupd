#!/usr/bin/env python3
# pylint: disable=invalid-name,missing-docstring
#
# Copyright 2024 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: LGPL-2.1-or-later

import struct
import sys
import binascii

from pcapng import FileScanner


def parse_pkt(block, title: str, offset: int):
    (
        st_id,
        st_type,
        st_size,
        rc_ctrl,
        rc_sts,
        rc_offset,
        rc_length,
    ) = struct.unpack_from("<BBBxxBBLL", block.packet_data, offset=offset)
    buf: bytes = block.packet_data[offset + 15 : offset + 15 + rc_length]
    if title == "GET" and rc_ctrl & 0x80:
        title = "get"
    print(
        f"{title} {st_id}:{st_type}:{st_size} "
        f"CTRL:0x{rc_ctrl:02X} STS:0x{rc_sts:02X} "
        f"OFFSET:0x{rc_offset:08X} LENGTH:0x{rc_length:02X}, "
        f"DATA:{binascii.hexlify(buf)}"
    )


def parse_file(filename: str):
    with open(filename, "rb") as f:
        scanner = FileScanner(f)
        for block in scanner:
            try:
                # print(block)
                if block.packet_len == 98:
                    parse_pkt(block, title="SET", offset=36)
                elif block.packet_len == 90:
                    parse_pkt(block, title="GET", offset=28)
            except AttributeError:
                pass


if __name__ == "__main__":
    parse_file(sys.argv[1])
