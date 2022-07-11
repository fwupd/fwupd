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


class ReturnValidator:
    def __init__(self):
        self.warnings: List[str] = []

    def parse(self, fn: str) -> None:

        with open(fn, "rb") as f:
            infunc = False
            has_parent_finalize = False
            for line in f.read().decode().split("\n"):

                # found the function, but ignore the prototype
                if line.find("_finalize(") != -1:
                    if line.endswith(";"):
                        continue
                    infunc = True
                    continue

                # got it
                if line.find("->finalize(") != -1:
                    has_parent_finalize = True
                    continue

                # finalize is done
                if infunc and line.startswith("}"):
                    if not has_parent_finalize:
                        self.warnings.append(
                            "{} did not have parent ->finalize()".format(fn)
                        )
                    break


def test_files():

    # test all C source files
    validator = ReturnValidator()
    for fn in glob.glob("**/*.c", recursive=True):
        if fn.startswith("dist/") or fn.startswith("subprojects/"):
            continue
        validator.parse(fn)
    for warning in validator.warnings:
        print(warning)

    return 1 if validator.warnings else 0


if __name__ == "__main__":

    # all done!
    sys.exit(test_files())
