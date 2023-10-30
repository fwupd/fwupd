#!/usr/bin/python3
#
# Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: LGPL-2.1+

import glob
import os
import sys
import subprocess


def __get_license(fn: str) -> str:
    with open(fn, "r") as f:
        for line in f.read().split("\n"):
            if line.find("SPDX-License-Identifier:") > 0:
                return line.split(":")[1]
    return ""


def test_files() -> int:

    rc: int = 0
    for fn in glob.glob("**/*.rs", recursive=True):
        if "meson-private" in fn:
            continue
        if "venv" in fn:
            continue
        if fn.startswith("subprojects"):
            continue
        if fn.startswith("dist"):
            continue
        try:
            subprocess.check_output(["rustfmt", "--quiet", "--check", fn])
        except FileNotFoundError as e:
            break
        except subprocess.CalledProcessError as e:
            print(str(e))
            rc = 1
    return rc


if __name__ == "__main__":

    # all done!
    sys.exit(test_files())
