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

    rc: int = 0

    # test all meson files
    for fn in glob.glob("**/meson.build", recursive=True) + glob.glob(
        "**/meson_options.txt", recursive=True
    ):

        # we do not care
        if fn.startswith("subprojects"):
            continue
        if fn.startswith("build"):
            continue
        if fn.startswith("dist"):
            continue
        if fn.startswith("muon"):
            continue
        try:
            _ = subprocess.run(
                ["muon", "fmt", "-c", "contrib/muon_fmt.ini", "-q", fn], check=True
            )
        except FileNotFoundError as e:
            print(f"Skipping check due to {str(e)}")
            break
        except subprocess.CalledProcessError:
            print(f"{fn} failed muon fmt")
            rc = 1

    return rc


if __name__ == "__main__":

    # all done!
    sys.exit(test_files())
