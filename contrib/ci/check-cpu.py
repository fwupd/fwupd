#!/usr/bin/env python3
# pylint: disable=invalid-name,missing-module-docstring,missing-function-docstring
#
# Copyright 2025 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: LGPL-2.1-or-later


import argparse
import os
import sys
import subprocess


def _check(cwd: str, argv: list[str], limit: int = 0) -> int:
    # prime cache
    try:
        subprocess.check_output(argv, cwd=cwd)
    except subprocess.CalledProcessError as e:
        print(e)
        return 1
    try:
        rc = subprocess.run(
            ["valgrind", "--tool=callgrind"] + argv,
            cwd=cwd,
            stderr=subprocess.PIPE,
            stdout=subprocess.PIPE,
            encoding="utf-8",
            check=True,
        )
    except subprocess.CalledProcessError as e:
        print(e)
        return 1
    value: int = 0
    for line in rc.stderr.split("\n"):
        if line.find("Collected : ") != -1:
            value = int(line.split(" ")[3])
    if limit and value > limit * 1_000_000:
        print(f"CPU usage was {value//1_000_000}Mcycles (limit of {limit}Mcycles)")
        return 1
    print(f"CPU usage was {value//1_000_000}Mcycles")
    return 0


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Check CPU usage")
    parser.add_argument("--limit", type=int, help="CPU limit in Mcycles")
    parser.add_argument("command", nargs="+", help="Command to run and measure")
    args = parser.parse_args()
    try:
        sys.exit(
            _check(
                cwd=os.path.dirname(sys.argv[0]), argv=args.command, limit=args.limit
            )
        )
    except IndexError:
        parser.print_help()
        sys.exit(1)
