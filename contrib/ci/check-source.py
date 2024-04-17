#!/usr/bin/python3
# pylint: disable=invalid-name,missing-module-docstring,missing-function-docstring
#
# Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: LGPL-2.1+

import glob
import sys


def test_files() -> int:

    # test all C and H files
    rc: int = 0
    for fn in glob.glob("**/*.[c|h]", recursive=True):
        if fn.startswith("subprojects"):
            continue
        if fn.startswith("build"):
            continue
        if fn.startswith("dist"):
            continue
        if fn.startswith("contrib/ci"):
            continue
        with open(fn, "rb") as f:
            for line in f.read().decode().split("\n"):
                if line.find("nocheck") != -1:
                    continue
                for token, msg in {
                    "cbor_get_uint8(": "Use cbor_get_int() instead",
                    "cbor_get_uint16(": "Use cbor_get_int() instead",
                    "cbor_get_uint32(": "Use cbor_get_int() instead",
                    "g_error(": "Use GError instead",
                    "g_byte_array_free_to_bytes(": "Use g_bytes_new() instead",
                }.items():
                    if line.find(token) != -1:
                        print(
                            f"{fn} contains blocked token {token}: {msg} -- use a nocheck comment to ignore"
                        )
                        rc = 1
    return rc


if __name__ == "__main__":

    # all done!
    sys.exit(test_files())
