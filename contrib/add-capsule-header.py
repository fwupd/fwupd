#!/usr/bin/python3
#
# Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: LGPL-2.1+

import sys
import uuid
import argparse
import ctypes

CAPSULE_FLAGS_PERSIST_ACROSS_RESET = 0x00010000
CAPSULE_FLAGS_POPULATE_SYSTEM_TABLE = 0x00020000
CAPSULE_FLAGS_INITIATE_RESET = 0x00040000

def add_header(infile, outfile, gd, fl=None):
    # parse GUID from command line
    try:
        guid = uuid.UUID(gd)
    except ValueError as e:
        print(e)
        return 1
    import struct
    try:
        with open(infile, 'rb') as f:
            bin_data = f.read()
    except FileNotFoundError as e:
        print(e)
        return 1

    # check if already has header
    hdrsz = struct.calcsize('<16sIII')
    if len(bin_data) >= hdrsz:
        hdr = struct.unpack('<16sIII', bin_data[:hdrsz])
        imgsz = hdr[3]
        if imgsz == len(bin_data):
            print('Replacing existing CAPSULE_HEADER of:')
            guid_mixed = uuid.UUID(bytes_le=hdr[0])
            hdrsz_old = hdr[1]
            flags = hdr[2]
            print('GUID:      %s' % guid_mixed)
            print('HdrSz:     0x%04x' % hdrsz_old)
            print('Flags:     0x%04x' % flags)
            print('PayloadSz: 0x%04x' % imgsz)
            bin_data = bin_data[hdrsz_old:]

    # set header flags
    flags = CAPSULE_FLAGS_PERSIST_ACROSS_RESET | CAPSULE_FLAGS_INITIATE_RESET | CAPSULE_FLAGS_POPULATE_SYSTEM_TABLE
    if fl:
        flags = int(fl, 16)

    # build update capsule header
    hdrsz = 4096
    imgsz = hdrsz + len(bin_data)
    hdr = ctypes.create_string_buffer(hdrsz)
    struct.pack_into('<16sIII', hdr, 0, guid.bytes_le, hdrsz, flags, imgsz)
    with open(outfile, 'wb') as f:
        f.write(hdr)
        f.write(bin_data)
    print('Wrote capsule %s' % outfile)
    print('GUID:      %s' % guid)
    print('HdrSz:     0x%04x' % hdrsz)
    print('Flags:     0x%04x' % flags)
    print('PayloadSz: 0x%04x' % imgsz)
    return 0

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Add capsule header on firmware')
    parser.add_argument('--guid', help='GUID of the device', required=True)
    parser.add_argument('--bin', help='Path to the .bin file', required=True)
    parser.add_argument('--cap', help='Output capsule file path', required=True)
    parser.add_argument('--flags', help='Flags, e.g. 0x40000', default=None)
    args = parser.parse_args()

    sys.exit(add_header(args.bin, args.cap, args.guid, args.flags))
