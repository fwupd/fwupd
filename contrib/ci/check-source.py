#!/usr/bin/python3
# pylint: disable=invalid-name,missing-module-docstring,missing-function-docstring
#
# Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: LGPL-2.1-or-later

import glob
import sys


def test_files() -> int:
    # test all C and H files
    rc: int = 0
    for fn in sorted(glob.glob("**/*.[c|h]", recursive=True)):
        if fn.startswith("subprojects"):
            continue
        if fn.startswith("build"):
            continue
        if fn.startswith("dist"):
            continue
        if fn.startswith("contrib/ci"):
            continue
        if fn.startswith("venv"):
            continue
        with open(fn, "rb") as f:
            linecnt_g_set_error: int = 0
            for linecnt, line in enumerate(f.read().decode().split("\n")):
                if line.find("nocheck") != -1:
                    continue
                for token, msg in {
                    "g_error(": "Use GError instead",
                    "g_byte_array_free_to_bytes(": "Use g_bytes_new() instead",
                }.items():
                    if line.find(token) != -1:
                        print(
                            f"{fn} contains blocked token {token}: {msg} -- "
                            "use a nocheck comment to ignore"
                        )
                        rc = 1

                # do not use G_IO_ERROR internally
                if line.find("g_set_error") != -1:
                    linecnt_g_set_error = linecnt
                if linecnt - linecnt_g_set_error < 5:
                    for error_domain in ["G_IO_ERROR", "G_FILE_ERROR"]:
                        if line.find(error_domain) != -1:
                            print(
                                f"{fn} uses g_set_error() without using FWUPD_ERROR: -- "
                                "use a nocheck comment to ignore"
                            )
                            rc = 1

    return rc


if __name__ == "__main__":
    # all done!
    sys.exit(test_files())
