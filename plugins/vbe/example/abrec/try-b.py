#!/usr/bin/env python3
#
# Copyright 2026 Simon Glass <sjg@chromium.org>
#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
"""Update the state as the firmware does when it boots B

This is for trying vbe-abrec on the host device, where there is no firmware to
try the update. It uses up one of the tries of B and records that B is being
tried, then rewrites the CRC.

Usage: try-b.py [<storage> [<state-offset>]]
"""

import sys

# the state is a power of two from 0x10 to 0x40 bytes, as its header says
STATE_MIN = 0x10
STATE_MAX = 0x40

TRY_COUNT_MASK = 0x3
TRY_B = 1 << 2
RECOVERY = 1 << 3
RESULT_SHIFT = 4
RESULT_MASK = 3 << RESULT_SHIFT
RESULT_TRYING = 1
PICK_SHIFT = 6
PICK_MASK = 3 << PICK_SHIFT
PICK_B = 1


def crc8(data):
    """CRC8 with polynomial 0x07, as used for the state"""
    crc = 0
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc


def main(argv):
    storage = argv[1] if len(argv) > 1 else "/tmp/testfw"
    offset = int(argv[2], 0) if len(argv) > 2 else 0x1000
    with open(storage, "r+b") as fd:
        # only read and write as much state as the header says there is
        fd.seek(offset)
        header = fd.read(2)
        size = 1 << (header[1] >> 4)
        if (header[1] & 0xF) != 1 or not STATE_MIN <= size <= STATE_MAX:
            print("state is not valid")
            return 1
        fd.seek(offset)
        state = bytearray(fd.read(size))
        if crc8(state[1:]) != state[0]:
            print("state is not valid")
            return 1
        flags = int.from_bytes(state[8:12], "little")
        count = flags & TRY_COUNT_MASK
        if flags & RECOVERY:
            print("recovery is requested, so the firmware boots that instead")
            return 1
        if not flags & TRY_B or not count:
            print("B is not waiting to be tried")
            return 1
        flags &= ~(TRY_COUNT_MASK | RESULT_MASK | PICK_MASK)
        flags |= (count - 1) | RESULT_TRYING << RESULT_SHIFT | PICK_B << PICK_SHIFT
        state[8:12] = flags.to_bytes(4, "little")
        state[0] = crc8(state[1:])
        fd.seek(offset)
        fd.write(state)
    print(f"trying B, {count - 1} tries left")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
