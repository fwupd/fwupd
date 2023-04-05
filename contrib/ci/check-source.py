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
            src = f.read().decode()
        for token, msg in {"g_error(": "Use GError instead"}.items():
            if src.find(token) != -1:
                print(f"{fn} contains blocked token {token}: {msg}")
                rc = 1
    return rc


if __name__ == "__main__":

    # all done!
    sys.exit(test_files())
