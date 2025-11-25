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
            ["valgrind"] + argv,
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
        if line.find("in use at exit: ") != -1:
            value = int(line.split(" ")[9].strip().replace(",", ""))
    if limit and value > limit * 1024:
        print(f"RSS usage was {value//1024}kB (limit of {limit}kB)")
        return 1
    print(f"RSS usage was {value//1024}kB")
    return 0


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Check RSS usage")
    parser.add_argument("--limit", type=int, help="RSS limit in kB")
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
