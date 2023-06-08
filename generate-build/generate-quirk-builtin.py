#!/usr/bin/python3
# pylint: disable=invalid-name,missing-docstring
#
# Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: LGPL-2.1+

import argparse
import gzip
from typing import List

if __name__ == "__main__":

    parser = argparse.ArgumentParser()
    parser.add_argument("output", action="store", type=str, help="output")
    parser.add_argument("input", nargs="*", help="input")
    args = parser.parse_args()

    lines: List[str] = []
    for fn in args.input:
        with open(fn, "rb") as f:
            for line in f.read().decode().split("\n"):
                if not line:
                    continue
                if line.startswith("#"):
                    continue
                lines.append(line)
    with gzip.open(args.output, "wb") as f:
        f.write("\n".join(lines).encode())
