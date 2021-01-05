#!/usr/bin/python3
# SPDX-License-Identifier: LGPL-2.1+

import sys
import struct

if __name__ == '__main__':

    # SignatureType
    buf = b'0' * 16

    # SignatureListSize
    buf += struct.pack('<I', 16 + 4 + 4 + 4 + 16 + 32)

    # SignatureHeaderSize
    buf += struct.pack('<I', 0)

    # SignatureSize
    buf += struct.pack('<I', 16 + 32)

    # SignatureOwner
    buf += b'1' * 16

    # SignatureData
    buf += b'2' * 32

    with open(sys.argv[1], 'wb') as f:
        f.write(buf)
