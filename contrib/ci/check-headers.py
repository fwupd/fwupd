#!/usr/bin/python3
# pylint: disable=invalid-name,missing-module-docstring,missing-function-docstring
#
# Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: LGPL-2.1+

import glob
import sys
import os
from typing import List


def __get_includes(fn: str) -> List[str]:
    includes: List[str] = []
    with open(fn, "r") as f:
        for line in f.read().split("\n"):
            if line.find("#include") == -1:
                continue
            for char in ["<", ">", '"']:
                line = line.replace(char, "")
            for char in ["\t"]:
                line = line.replace(char, " ")
            includes.append(line.split(" ")[-1])
    return includes


def test_files() -> int:

    rc: int = 0

    toplevel_headers = ["libfwupd/fwupd.h", "libfwupdplugin/fwupdplugin.h"]
    toplevel_headers_nopath = [os.path.basename(fn) for fn in toplevel_headers]

    for toplevel_header in toplevel_headers:

        toplevel_fn = os.path.basename(toplevel_header)
        toplevel_includes = __get_includes(toplevel_header)
        toplevel_includes_nopath = [os.path.basename(fn) for fn in toplevel_includes]

        # test all C and H files
        for fn in glob.glob("**/*.[c|h]", recursive=True):
            includes = __get_includes(fn)

            # we do not need both toplevel haders
            if set(toplevel_headers_nopath).issubset(set(includes)):
                print(
                    "{} contains both {}".format(fn, ", ".join(toplevel_headers_nopath))
                )

            # toplevel not listed
            if toplevel_fn not in includes:
                continue

            # includes toplevel and *also* something listed in the toplevel
            for include in includes:
                if include in toplevel_includes or include in toplevel_includes_nopath:
                    print(
                        "{} contains {} but also includes {}".format(
                            fn, toplevel_fn, include
                        )
                    )
                    rc = 1

    return rc


if __name__ == "__main__":

    # all done!
    sys.exit(test_files())
