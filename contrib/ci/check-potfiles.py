#!/usr/bin/python3
# pylint: disable=invalid-name,missing-docstring,consider-using-f-string
# pylint: disable=too-few-public-methods
#
# Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: LGPL-2.1+

import glob
import sys

from typing import List


def test_files():

    # compare with POTFILES.in
    with open("po/POTFILES.in", "rb") as f:
        potfiles_fns: List[str] = f.read().decode().split("\n")
    for fn in sorted(
        glob.glob("src/*.c")
        + glob.glob("plugins/*/*.c")
        + glob.glob("policy/*.policy.in")
        + glob.glob("data/*/*.xml")
        + glob.glob("libfwupdplugin/tests/bios-attrs/*/*.txt")
    ):
        if (
            fn.startswith("dist/")
            or fn.startswith("subprojects/")
            or fn.startswith("build/")
        ):
            continue
        with open(fn, "rb") as f:
            blob = f.read().decode()
        if blob.find('_("') != -1 or blob.find("TRANSLATORS") != -1:
            if fn not in potfiles_fns:
                print("{} is missing from po/POTFILES.in".format(fn))
                return 1

    # success
    return 0


if __name__ == "__main__":

    # all done!
    sys.exit(test_files())
