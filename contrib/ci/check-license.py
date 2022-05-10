#!/usr/bin/python3
#
# Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
# Copyright (C) 2021 Mario Limonciello <superm1@gmail.com>
#
# SPDX-License-Identifier: LGPL-2.1+

import glob
import os
import sys


def __get_license(fn: str) -> str:
    with open(fn, "r") as f:
        for line in f.read().split("\n"):
            if line.find("SPDX-License-Identifier:") > 0:
                return line.split(":")[1]
    return ""


def test_files() -> int:

    rc: int = 0
    build_dirs = [os.path.dirname(cf) for cf in glob.glob("**/config.h")]

    for fn in glob.glob("**/*.[c|h|py|sh]", recursive=True):
        if "meson-private" in fn:
            continue
        if os.path.isdir(fn):
            continue
        if fn.startswith(tuple(build_dirs)):
            continue
        if fn.startswith("subprojects"):
            continue
        if fn.startswith("dist"):
            continue
        lic = __get_license(fn)
        if not lic:
            print("{} does not specify a license".format(fn))
            rc = 1
            continue
        if not "GPL" in lic:
            print("{} does not contain LGPL or GPL ({})".format(fn, lic))
            rc = 1
            continue
    return rc


if __name__ == "__main__":

    # all done!
    sys.exit(test_files())
