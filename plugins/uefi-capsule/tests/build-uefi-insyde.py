#!/usr/bin/env python3
# Copyright 2025 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# pylint: disable=invalid-name,missing-module-docstring

import sys
import struct
import uuid

buf = b""
buf += struct.pack("<4s", b"UEFI")  # signature
buf += struct.pack("<I", 70)  # length of buf
buf += struct.pack("<B", 1)  # revision
buf += struct.pack("<B", 0x65)  # checksum
buf += struct.pack("<6s", b"XXXXXX")  # oem_id
buf += struct.pack("<8s", b"YYYYYYYY")  # oem_table_id
buf += struct.pack("<I", 1)  # oem_revision
buf += struct.pack("<4s", b"FWPD")  # asl_compiler_id
buf += struct.pack("<I", 1)  # asl_compiler_revision

# then GUID @0x24
buf += uuid.UUID("9d4bf935-a674-4710-ba02-bf0aa1758c7b").bytes_le

# then jumk data
buf += struct.pack("<4s", b"JUNK")

# add insyde blob
buf += struct.pack("<6s", b"$QUIRK")  # signature
buf += struct.pack("<I", 14)  # size
buf += struct.pack("<I", 1)  # flags, COD does not work

with open(sys.argv[1], "wb") as f:
    f.write(buf)
