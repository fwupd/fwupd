#!/usr/bin/python3
# pylint: disable=invalid-name,missing-module-docstring,missing-function-docstring
#
# Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: LGPL-2.1+

import glob
import sys
import subprocess


def test_files() -> int:

    fns = sys.argv[1:]
    if not fns:
        fns.append("./plugins")
        fns.append("./src")

    data = []

    # find all .o files
    for fn in fns:
        for fn in glob.glob(f"{fn}/**/*.o", recursive=True):
            print(f"Analyzing {fn}...")
            p = subprocess.run(["nm", fn], check=True, capture_output=True)
            # parse data
            for line in p.stdout.decode().split("\n"):
                line = line.rstrip()
                if len(line) == 0:
                    continue
                if line.endswith(".o:"):
                    continue
                t = line[17:18]
                if t == "b" or t == "t" or t == "r" or t == "d" or t == "a":
                    continue
                symb = line[19:]
                data.append((t, symb))

    # collect all the symbols defined
    symbs = []
    for t, symb in data:
        if t != "T":
            continue
        if symb.endswith("_get_type"):
            continue
        if symb.startswith("fu_struct_"):
            continue
        if symb.find("__proto__") != -1:
            continue
        if symb in ["main", "fu_plugin_init_vfuncs"]:
            continue
        if symb not in symbs:
            symbs.append(symb)

    # remove the ones used
    for t, symb in data:
        if t != "U":
            continue
        if symb in symbs:
            symbs.remove(symb)

    # display
    symbs.sort()
    for symb in symbs:
        print("Unused: ", symb)

    return 1 if symbs else 0


if __name__ == "__main__":

    # all done!
    sys.exit(test_files())
