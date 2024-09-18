#!/usr/bin/env python3
#
# Copyright 2024 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# pylint: disable=invalid-name,missing-docstring,too-few-public-methods

from typing import Dict
import os
import glob
import sys
import subprocess
import json
import argparse
from collections import defaultdict
from termcolor import colored


def _scan_file(fn: str) -> Dict[str, int]:
    new_map: Dict[str, int] = defaultdict(int)
    try:
        print(f"loading {fn}…")
        args = [
            "./src/fwupdtool",
            "firmware-parse",
            fn,
            "ifd-firmware",
            "--json",
            "--no-timestamp",
        ]
        p = subprocess.run(args, check=True, capture_output=True)
    except subprocess.CalledProcessError as e:
        print(f"{' '.join(args)}: {e}")
    else:
        for line in p.stdout.decode().split("\n"):
            new_map["Lines"] += 1
            if line.find("gtype=") == -1:
                continue
            sections = line.split('"')
            if not sections[1].startswith("Fu"):
                continue
            new_map[sections[1]] += 1
        for line in p.stderr.decode().split("\n"):
            if not line:
                continue
            new_map["WarningLines"] += 1
            print(line)
    return new_map


def _scan_dir(path: str, force_save: bool = False) -> bool:
    all_okay: bool = True
    needs_save: bool = False

    results: Dict[str, Dict[str, int]] = {}

    # support folders or paths
    if os.path.isdir(path):
        for fn in glob.glob(f"{path}/*.bin"):
            results[fn] = _scan_file(fn)
    else:
        results[path] = _scan_file(path)

    # go through each result
    print(f"    {os.path.basename(sys.argv[0])}:")
    for fn, new_map in results.items():
        try:
            with open(f"{fn}.json", "rb") as f:
                old_map = json.loads(f.read().decode())
        except FileNotFoundError:
            old_map = {}
        print(f"    {fn}")
        for key in sorted(set(list(old_map.keys()) + list(new_map.keys()))):
            cnt_old = old_map.get(key, 0)
            cnt_new = new_map.get(key, 0)
            if cnt_new > cnt_old:
                key_str: str = colored(key, "green")
                needs_save = True
            elif cnt_new < cnt_old:
                key_str = colored(key, "red")
                if key != "WarningLines":
                    all_okay = False
            else:
                continue
            print(f"    {key_str:36}: {cnt_old} -> {cnt_new}")

    # save new results if all better
    if (needs_save and all_okay) or force_save:
        for fn, new_map in results.items():
            with open(f"{fn}.json", "wb") as f:
                f.write(json.dumps(new_map, sort_keys=True, indent=4).encode())

    return all_okay


if __name__ == "__main__":
    rc: int = 0

    parser = argparse.ArgumentParser(
        prog="check-ifd-firmware", description="Check IFD firmware parsing"
    )
    parser.add_argument("paths", nargs="+")
    parser.add_argument(
        "--force-save", action="store_true", help="always save the json reports"
    )
    _args = parser.parse_args()
    for _path in _args.paths:
        if not _scan_dir(_path, force_save=_args.force_save):
            rc = 1
    sys.exit(rc)
