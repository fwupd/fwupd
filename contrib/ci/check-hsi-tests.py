#!/usr/bin/env python3
#
# Copyright 2026 Mario Limonciello <superm1@gmail.com>
#
# SPDX-License-Identifier: LGPL-2.1-or-later

import os
import sys


def test_files() -> int:
    rc: int = 0
    hsi_dir = "docs/hsi-tests.d"
    meson_build = os.path.join(hsi_dir, "meson.build")

    with open(meson_build) as f:
        meson_contents = f.read()

    for fn in sorted(os.listdir(hsi_dir)):
        if fn == "meson.build":
            continue
        if not fn.endswith(".json"):
            print(f"{hsi_dir}/{fn} does not have a .json extension")
            rc = 1
            continue
        if f"'{fn}'" not in meson_contents:
            print(f"{hsi_dir}/{fn} is missing from {meson_build}")
            rc = 1

    return rc


if __name__ == "__main__":
    # all done!
    sys.exit(test_files())
