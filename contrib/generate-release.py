#!/usr/bin/env python3
#
# Copyright 2023 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# pylint: disable=invalid-name,missing-docstring

import sys
import os
import subprocess
import datetime
from jinja2 import Environment, FileSystemLoader


def _get_last_release() -> str:
    return (
        subprocess.check_output(["git", "describe", "--tags", "--abbrev=0"])
        .decode()
        .replace("\n", "")
    )


def _get_next_release(last_tag: str) -> str:
    try:
        triplet: list[str] = last_tag.split(".")
        return f"{triplet[0]}.{triplet[1]}.{int(triplet[2])+1}"
    except IndexError:
        return last_tag


def _get_appstream_date() -> str:
    return datetime.datetime.now().strftime("%Y-%m-%d")


def _generate_release_notes(last_tag: str, next_tag: str) -> str:
    lines_feat: list[str] = []
    lines_bugs: list[str] = []
    lines_devs: list[str] = []
    for line in (
        subprocess.check_output(
            [
                "git",
                "log",
                "--format=%s",
                f"{last_tag}...",
            ]
        )
        .decode()
        .split("\n")
    ):
        if not line:
            continue
        if line.find("trivial") != -1:
            continue
        if line.find("Typo") != -1:
            continue
        if line.find("Merge") != -1:
            continue
        if line.find("build(deps)") != -1:
            continue
        if line in lines_feat or line in lines_bugs or line in lines_devs:
            continue
        if line.startswith("Add "):
            lines_feat.append(line)
            continue
        lines_bugs.append(line)

    env = Environment(
        loader=FileSystemLoader(os.path.dirname(os.path.realpath(__file__))),
        autoescape=False,
        keep_trailing_newline=False,
    )
    template = env.get_template("generate-release.xml")
    return template.render(
        {
            "version": next_tag,
            "date": _get_appstream_date(),
            "features": lines_feat,
            "bugs": lines_bugs,
            "devices": lines_devs,
        }
    )


if __name__ == "__main__":
    try:
        _last_tag: str = sys.argv[1]
    except IndexError:
        _last_tag: str = _get_last_release()
    try:
        _next_tag: str = sys.argv[2]
    except IndexError:
        _next_tag: str = _get_next_release(_last_tag)
    xml: str = _generate_release_notes(_last_tag, _next_tag)
    print(xml)
