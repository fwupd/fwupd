#!/usr/bin/python3
# pylint: disable=invalid-name,missing-module-docstring,missing-function-docstring
#
# Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
# Copyright (C) 2021 Mario Limonciello <superm1@gmail.com>
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
    for fn in glob.glob("**/*.[c|h]", recursive=True):
        includes = __get_includes(fn)

        # we do not care
        if fn.startswith("subprojects"):
            continue
        if fn.startswith("build"):
            continue
        if fn.startswith("dist"):
            continue
        if fn.startswith("contrib/ci"):
            continue
        if fn in [
            "libfwupd/fwupd-context-test.c",
            "libfwupd/fwupd-thread-test.c",
            "libfwupdplugin/fu-fuzzer-main.c",
        ]:
            continue

        if (
            fn.startswith("plugins")
            and not fn.endswith("self-test.c")
            and not fn.endswith("-tool.c")
        ):
            for include in includes:
                # check for using private header use in plugins
                if include.endswith("private.h"):
                    print("{} uses private header {}".format(fn, include))
                    rc = 1
                    continue

                # check for referring to anything but top level header
                if include in lib_headers or include in lib_headers_nopath:
                    print(
                        "{} contains {}, should only use top level includes".format(
                            fn, include
                        )
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

        # check for missing config.h
        if fn.endswith(".c") and "config.h" not in includes:
            print("{} does not include config.h".format(fn))
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
                    print(
                        "{} contains {} which is implied by {}".format(fn, value, key)
                    )
                    rc = 1

    return rc


if __name__ == "__main__":

    # all done!
    sys.exit(test_files())
