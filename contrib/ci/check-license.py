#!/usr/bin/env python3
#
# Copyright 2021 Richard Hughes <richard@hughsie.com>
# Copyright 2021 Mario Limonciello <superm1@gmail.com>
#
# SPDX-License-Identifier: LGPL-2.1-or-later

import glob
import os
import sys
import fnmatch


def test_files() -> int:
    rc: int = 0
    build_dirs = [os.path.dirname(cf) for cf in glob.glob("**/config.h")]

    for fn in (
        glob.glob("libfwupd/*.[c|h|py|rs]")
        + glob.glob("libfwupdplugin/.[c|h|py|rs]")
        + glob.glob("plugins/*/.[c|h|py|rs]")
        + glob.glob("src/.[c|h|py|rs]")
    ):
        if fn.endswith("check-license.py"):
            continue
        lic: str = ""
        cprts: list[str] = []
        lines: list[str] = []
        with open(fn) as f:
            for line in f.read().split("\n"):
                lines.append(line)
        if len(lines) < 2:
            continue
        for line in lines:
            if line.find("SPDX-License-Identifier:") != -1:
                lic = line.split(":")[1]
            if line.find("Copyright") != -1:
                cprts.append(line.strip())
        if not lic:
            print(f"{fn} does not specify a license")
            rc = 1
            continue
        if not cprts:
            print(f"{fn} does not specify any copyright")
            rc = 1
            continue
        if "LGPL-2.1-or-later" not in lic:
            print(f"{fn} does not contain LGPL-2.1-or-later ({lic})")
            rc = 1
            continue
        for cprt in cprts:
            for word in ["(C)", "(c)", "©", "  "]:
                if cprt.find(word) != -1:
                    print(f"{fn} should not contain {word} in the string {cprt}")
                    rc = 1
    return rc


if __name__ == "__main__":
    # all done!
    sys.exit(test_files())
