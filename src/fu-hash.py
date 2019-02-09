#!/usr/bin/python3
""" Builds a header for the plugins to include """

# pylint: disable=invalid-name,wrong-import-position,pointless-string-statement

"""
SPDX-License-Identifier: LGPL-2.1+
"""

import sys
import hashlib

def usage(return_code):
    """ print usage and exit with the supplied return code """
    if return_code == 0:
        out = sys.stdout
    else:
        out = sys.stderr
    out.write("usage: fu-hash.py <DAEMON> <HEADER>")
    sys.exit(return_code)

if __name__ == '__main__':
    if {'-?', '--help', '--usage'}.intersection(set(sys.argv)):
        usage(0)
    if len(sys.argv) != 3:
        usage(1)
    with open(sys.argv[1], 'rb') as f:
        buf = f.read()
        csum = hashlib.sha256(buf).hexdigest()
        with open(sys.argv[2], 'w') as f2:
            f2.write('#pragma once\n')
            f2.write('#define FU_BUILD_HASH "%s"\n' % csum)
