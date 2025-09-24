#!/usr/bin/env python3
#
# Copyright 2025 Thomas Mühlbacher <tmuehlbacher@posteo.net>
#
# SPDX-License-Identifier: LGPL-2.1-or-later


import argparse
import json
import logging
import os
import subprocess
import sys

from typing import Iterator


def parse_version(ver):
    return tuple(map(int, ver.split(".")))


# see https://github.com/mesonbuild/meson/pull/14890
def old_meson_missing_vapi_deps_tag() -> bool:
    version_str = subprocess.check_output(
        ["meson", "--version"],
        text=True,
    )
    return parse_version(version_str.strip()) <= parse_version("1.9.0")


def objects_with_tag(obj) -> Iterator[dict]:
    match obj:
        case dict(d):
            if "tag" in d:
                yield d
            for v in d.values():
                yield from objects_with_tag(v)
        case list(l):
            for i in l:
                yield from objects_with_tag(i)


def collect_tags(install_plan) -> list[str]:
    tags = set()

    for obj in objects_with_tag(install_plan):
        tags.add(obj["tag"])

    return sorted(["null" if t is None else t for t in tags])


def collect_files(install_plan, tag) -> list[str]:
    files = list()

    if tag == "null":
        tag = None

    for obj in objects_with_tag(install_plan):
        if obj["tag"] == tag:
            files.append(obj["destination"])

    return sorted(files)


def print_tags(install_plan):
    for tag in collect_tags(install_plan):
        print(tag)


def print_files(install_plan, tag):
    for file in collect_files(install_plan, tag):
        print(file)


def print_all(install_plan):
    for tag in collect_tags(install_plan):
        for file in collect_files(install_plan, tag):
            print(f"{tag}: {file}")


def check(install_plan) -> int:
    exit_code = 0
    tags = collect_tags(install_plan)

    # check for files missing install tag
    ignore_vapi_deps = old_meson_missing_vapi_deps_tag()
    for null_file in collect_files(install_plan, None):
        if ignore_vapi_deps and null_file.endswith("fwupd.deps"):
            logging.warning(f"missing meson install tag: {null_file}")
        else:
            logging.error(f"missing meson install tag: {null_file}")
            exit_code = 1

    # check for incorrect test file tags
    for tag in [t for t in tags if t != "tests"]:
        files = collect_files(install_plan, tag)
        files = [f for f in files if "installed-tests" in f]
        for f in files:
            logging.error(f"file should have 'tests' tag: {f}")
            exit_code = 1

    return exit_code


def main() -> int:
    parser = argparse.ArgumentParser(description="Meson install tag helper")
    parser.add_argument(
        "-C", dest="working_directory", default=".", help="Meson build directory"
    )
    parser.add_argument(
        "command",
        choices=[
            "check",
            "list",
            "list-files",
            "list-tags",
        ],
    )
    parser.add_argument("-t", "--tag", help="Optional tag for list-files command")

    args = parser.parse_args()

    install_plan = json.loads(
        subprocess.check_output(
            ["meson", "introspect", "--install-plan"],
            cwd=args.working_directory,
            text=True,
        )
    )

    exit_code = 0
    match args.command:
        case "check":
            exit_code = check(install_plan)
        case "list":
            print_all(install_plan)
        case "list-files":
            print_files(install_plan, args.tag)
        case "list-tags":
            print_tags(install_plan)

    return exit_code


if __name__ == "__main__":
    logging.basicConfig(
        level=logging.INFO, format="%(levelname)s: %(message)s", stream=sys.stderr
    )

    if "PRE_COMMIT" not in os.environ:
        sys.exit(main())

    if not os.path.exists("venv/build"):
        logging.info("no configured build directory, skipping check-meson-install-tag")
        sys.exit(0)

    install_plan = json.loads(
        subprocess.check_output(
            ["meson", "introspect", "--install-plan"],
            cwd="venv/build",
            text=True,
        )
    )

    if len(collect_files(install_plan, None)) > 10:
        logging.info("build dir is likely outdated, skipping check-meson-install-tag")
        sys.exit(0)

    check(install_plan)
    sys.exit(0)
