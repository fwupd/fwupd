#!/usr/bin/env python3
#
# Copyright 2021 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# pylint: disable=missing-module-docstring,missing-function-docstring,invalid-name,too-many-branches

import glob
import sys
from typing import List


def gtype_is_valid(gtype: str) -> bool:
    if not gtype:
        return False
    for token in ["Plugin", "Firmware", "Backend", "Image"]:
        if gtype.endswith(token):
            return False
    return True


def test_files() -> int:
    rc: int = 0

    gtypes: List[str] = []

    # find the possible GTypes
    for fn in glob.glob("libfwupdplugin/fu-*-device.c") + glob.glob("plugins/*/fu-*.c"):
        with open(fn) as f:
            for line in f.read().split("\n"):
                gtype: str = ""
                if line.startswith("G_DEFINE_TYPE("):
                    gtype = line[14:].split(",")[0]
                if line.startswith("G_DEFINE_TYPE_WITH_PRIVATE("):
                    gtype = line[27:].split(",")[0]
                if gtype_is_valid(gtype):
                    gtypes.append(gtype)

    # check each quirk file
    for fn in glob.glob("data/*.quirk") + glob.glob("plugins/*/*.quirk"):
        with open(fn) as f:
            for line in f.read().split("\n"):
                if line.startswith(" ") or line.endswith(" "):
                    print(f"{fn} has leading or trailing whitespace: {line}")
                    rc = 1
                    continue
                if not line or line.startswith("#"):
                    continue
                if line.startswith("["):
                    if not line.endswith("]"):
                        print(f"{fn} has invalid section header: {line}")
                        rc = 1
                        continue
                else:
                    sections = line.split(" = ")
                    if len(sections) != 2:
                        print(f"{fn} has invalid line: {line}")
                        rc = 1
                        continue
                    if sections[0] == "GType" and sections[1] not in gtypes:
                        print(f"{fn} has invalid GType: {line}")
                        rc = 1
                        continue
                    for section in sections:
                        if section.strip() != section:
                            print(f"{fn} has invalid spacing: {line}")
                            rc = 1
                            break
    return rc


if __name__ == "__main__":
    # all done!
    sys.exit(test_files())
