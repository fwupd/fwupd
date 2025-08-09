#!/usr/bin/env python3

import argparse
import json
import logging
import subprocess
import sys

from typing import Generator


logging.basicConfig(
    level=logging.INFO, format="%(levelname)s: %(message)s", stream=sys.stderr
)


def objects_with_tag(obj) -> Generator[dict]:
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
    r = set()

    for obj in objects_with_tag(install_plan):
        r.add(obj["tag"])

    return sorted(map(lambda i: "null" if i is None else i, r))


def collect_files(install_plan, tag) -> list[str]:
    r = list()

    if tag == "null":
        tag = None

    for obj in objects_with_tag(install_plan):
        if obj["tag"] == tag:
            r.append(obj["destination"])

    return sorted(r)


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


def check(install_plan):
    exit_code = 0
    tags = collect_tags(install_plan)

    # Check for files missing install tag
    for null_file in collect_files(install_plan, None):
        logging.error(f"missing meson install tag: {null_file}")
        exit_code = 1

    # Check for incorrect test file tags
    for tag in filter(lambda t: t != "tests", tags):
        files = collect_files(install_plan, tag)
        files = [f for f in files if "installed-tests" in f]
        for f in files:
            logging.error(f"file should have 'tests' tag: {f}")
            exit_code = 1

    sys.exit(exit_code)


def main():
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
    parser.add_argument(
        "tag", nargs="?", default=None, help="Optional tag for list-files command"
    )

    args = parser.parse_args()

    # Capture meson introspect output directly
    install_plan = json.loads(
        subprocess.check_output(
            ["meson", "introspect", "--install-plan"],
            cwd=args.working_directory,
            text=True,
        )
    )

    match args.command:
        case "check":
            check(install_plan)
        case "list":
            print_all(install_plan)
        case "list-files":
            print_files(install_plan, args.tag)
        case "list-tags":
            print_tags(install_plan)


if __name__ == "__main__":
    main()
