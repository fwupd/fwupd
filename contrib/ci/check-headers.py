#!/usr/bin/env python3
# pylint: disable=invalid-name,missing-module-docstring,missing-function-docstring
#
# Copyright 2021 Richard Hughes <richard@hughsie.com>
# Copyright 2021 Mario Limonciello <superm1@gmail.com>
#
# SPDX-License-Identifier: LGPL-2.1-or-later

import glob
import sys
import os
from typing import List


def __get_includes(fn: str) -> List[str]:
    includes: List[str] = []
    with open(fn) as f:
        for line in f.read().split("\n"):
            if line.find("#include") == -1:
                continue
            if line.find("waive-pre-commit") > 0:
                continue
            for char in ["<", ">", '"']:
                line = line.replace(char, "")
            for char in ["\t"]:
                line = line.replace(char, " ")
            includes.append(line.split(" ")[-1])
    return includes


def test_files() -> int:
    rc: int = 0

    lib_headers1 = glob.glob("libfwupd/*.h")
    lib_headers1.remove("libfwupd/fwupd.h")

    lib_headers2 = glob.glob("libfwupdplugin/*.h")
    lib_headers2.remove("libfwupdplugin/fwupdplugin.h")

    toplevel_headers = ["libfwupd/fwupd.h", "libfwupdplugin/fwupdplugin.h"]
    toplevel_headers_nopath = [os.path.basename(fn) for fn in toplevel_headers]
    lib_headers = lib_headers1 + lib_headers2
    lib_headers_nopath = [os.path.basename(fn) for fn in lib_headers]

    # test all C and H files
    for fn in (
        glob.glob("libfwupd/*.[c|h]")
        + glob.glob("libfwupdplugin/*.[c|h]")
        + glob.glob("plugins/*/*.[c|h]")
        + glob.glob("src/*.[c|h]")
    ):
        # we do not care
        if fn in [
            "libfwupd/fwupd-context-test.c",
            "libfwupd/fwupd-thread-test.c",
            "libfwupdplugin/fu-fuzzer-main.c",
        ]:
            continue

        includes = __get_includes(fn)
        if (
            fn.startswith("plugins")
            and not fn.endswith("self-test.c")
            and not fn.endswith("tool.c")
        ):
            for include in includes:
                # check for using private header use in plugins
                if include.endswith("private.h"):
                    print(f"{fn} uses private header {include}")
                    rc = 1
                    continue

                # check for referring to anything but top level header
                if include in lib_headers or include in lib_headers_nopath:
                    print(
                        f"{fn} contains {include}, should only use top level includes"
                    )
                    rc = 1

        # check for double top level headers
        for toplevel_header in toplevel_headers:
            toplevel_fn = os.path.basename(toplevel_header)
            toplevel_includes = __get_includes(toplevel_header)
            toplevel_includes_nopath = [
                os.path.basename(fn) for fn in toplevel_includes
            ]

            # we do not need both toplevel headers
            if set(toplevel_headers_nopath).issubset(set(includes)):
                print(f"{fn} contains both {', '.join(toplevel_headers_nopath)}")

            # toplevel not listed
            if toplevel_fn not in includes:
                continue

            # includes toplevel and *also* something listed in the toplevel
            for include in includes:
                if include in toplevel_includes or include in toplevel_includes_nopath:
                    print(f"{fn} contains {toplevel_fn} but also includes {include}")
                    rc = 1

        # check for missing config.h
        if fn.endswith(".c") and "config.h" not in includes:
            print(f"{fn} does not include config.h")
            rc = 1

        # check for one header implying the other
        implied_headers = {
            "fu-common.h": ["xmlb.h"],
            "fwupdplugin.h": [
                "gio/gio.h",
                "glib.h",
                "glib-object.h",
                "xmlb.h",
                "fwupd.h",
            ]
            + lib_headers1,
            "gio/gio.h": ["glib.h", "glib-object.h"],
            "glib-object.h": ["glib.h"],
            "json-glib/json-glib.h": ["glib.h", "glib-object.h"],
            "xmlb.h": ["gio/gio.h"],
        }
        for key, values in implied_headers.items():
            for value in values:
                if key in includes and value in includes:
                    print(f"{fn} contains {value} which is implied by {key}")
                    rc = 1

    return rc


if __name__ == "__main__":
    # all done!
    sys.exit(test_files())
