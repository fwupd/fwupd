#!/usr/bin/env python3
#
# Copyright 2021 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: LGPL-2.1-or-later

import glob
import sys


def test_files() -> int:
    rc: int = 0

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
                    for section in sections:
                        if section.strip() != section:
                            print(f"{fn} has invalid spacing: {line}")
                            rc = 1
                            break
    return rc


if __name__ == "__main__":
    # all done!
    sys.exit(test_files())
