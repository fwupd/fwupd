#!/usr/bin/python3
# pylint: disable=invalid-name,missing-docstring,consider-using-f-string
#
# Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: LGPL-2.1+

import os
import sys
import argparse
from typing import List, Dict
from jinja2 import Environment, FileSystemLoader, select_autoescape


def _is_md_title(line: str) -> bool:
    if not line:
        return False
    for char in line:
        if char not in ["#", "-", "="]:
            return False
    return True


def _replace_bookend(line: str, search: str, replace_l: str, replace_r: str) -> str:

    try:
        while line.find(search) != -1:
            it = iter(line.split(search, maxsplit=2))
            line_tmp: str = ""
            for token_before in it:
                line_tmp += token_before
                line_tmp += replace_l
                line_tmp += next(it)  # token_mid
                line_tmp += replace_r
                line_tmp += next(it)  # token_after
            line = line_tmp
    except StopIteration:
        pass
    return line


def _convert_md_to_man(data: str) -> str:

    sections = data.split("\n\n")
    troff_lines: List[str] = []

    # ignore the docgen header
    if sections[0].startswith("---"):
        sections = sections[1:]

    # header
    split = sections[0].split(" ", maxsplit=4)
    if split[0] != "%" or split[3] != "|":
        print(
            "no man header detected, expected something like "
            "'% fwupdagent(1) 1.2.5 | fwupdagent man page' and got {}".format(
                sections[0]
            )
        )
        sys.exit(1)
    man_cmd = split[1][:-3]
    man_sect = int(split[1][-2:-1])
    troff_lines.append(f'.TH "{man_cmd}" "{man_sect}" "" {split[2]} "{split[4]}"')
    troff_lines.append(".hy")  # hyphenate

    # content
    for section in sections[1:]:
        lines = section.split("\n")
        sectkind: str = ".PP"  # begin a new paragraph
        sectalign: int = 4

        # convert markdown headers to section headers
        if _is_md_title(lines[-1]):
            lines = lines[:-1]
            sectkind = ".SH"

        # join long lines
        line = ""
        for line_tmp in lines:
            if not line_tmp:
                continue
            if line_tmp.startswith("| "):
                line_tmp = line_tmp[2:]
            if line_tmp.startswith("  "):
                line_tmp = line_tmp[2:]
                sectalign = 8
            elif line_tmp.startswith("* "):
                sectalign = 8
            line_tmp = line_tmp.replace("\\-", "-")
            line += line_tmp
            if line_tmp.endswith("."):
                line += " "
            line += " "

        # make bold, and add smart quotes
        line = _replace_bookend(line, "**", "\\f[B]", "\\f[R]")
        line = _replace_bookend(line, "`", "\\f[B]", "\\f[R]")
        line = _replace_bookend(line, '"', "“", "”")

        # add troff
        if sectalign != 4:
            troff_lines.append(f".RS {sectalign}")
        troff_lines.append(sectkind)
        troff_lines.append(line)
        if sectalign != 4:
            troff_lines.append(".RE")

    # success
    return "\n".join(troff_lines)


def _add_defines(defines: Dict[str, str], fn: str) -> None:
    with open(fn, "rb") as f:
        for line in f.read().decode().split("\n"):
            if not line.startswith("#define"):
                continue
            if line.find("_CONFIG_DEFAULT_") == -1:
                continue
            sections: List[str] = []
            for section in line[7:].split(" "):
                section = section.strip()
                if section:
                    if section == "TRUE":
                        section = "true"
                    if section == "FALSE":
                        section = "false"
                    if section == "NULL":
                        section = ""
                    if section.startswith('"') and section.endswith('"'):
                        section = section[1:-1]
                    sections.append(section)
            if len(sections) >= 2:
                defines[sections[0]] = sections[1]


if __name__ == "__main__":

    parser = argparse.ArgumentParser()
    parser.add_argument("-o", "--output")
    parser.add_argument(
        "-r", "--replace", action="append", nargs=2, metavar=("symbol", "version")
    )
    parser.add_argument("-d", "--defines", action="append")
    args, argv = parser.parse_known_args()
    if len(argv) != 1:
        print(f"usage: {sys.argv[0]} MARKDOWN [-o TROFF]\n")
        sys.exit(1)

    # load in #defines to populate the defaults
    subst: Dict[str, str] = {}
    if args.defines:
        for fn_define in args.defines:
            try:
                _add_defines(subst, fn_define)
            except FileNotFoundError:
                print(f"{fn_define} not found")
                sys.exit(1)

    # static defines
    if args.replace:
        for key, value in args.replace:
            subst[key] = value

    # use Jinja2 to process as a template
    env = Environment(
        loader=FileSystemLoader(os.path.dirname(argv[0])),
        autoescape=select_autoescape(),
        keep_trailing_newline=True,
    )
    template = env.get_template(os.path.basename(argv[0]))
    out = _convert_md_to_man(template.render(subst))

    # success
    if args.output:
        with open(args.output, "wb") as f_out:
            f_out.write(out.encode())
    else:
        print(out)
