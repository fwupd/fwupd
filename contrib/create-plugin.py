#!/usr/bin/python3
#
# Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: LGPL-2.1+
#
# pylint: disable=invalid-name,missing-docstring,consider-using-f-string

import os
import datetime
import argparse
import glob
import sys
from jinja2 import Environment, FileSystemLoader, select_autoescape

subst = {}


def _fix_case(value: str) -> str:

    return value[0].upper() + value[1:].lower()


def _subst_add_string(key: str, value: str) -> None:

    # sanity check
    if not value.isascii():
        raise NotImplementedError("{} can only be ASCII, got {}".format(key, value))
    if len(value) < 3:
        raise NotImplementedError(
            "{} has to be at least length 3, got {}".format(key, value)
        )

    subst[key] = value
    subst[key.lower()] = value.lower()
    subst[key.upper()] = value.upper()


def _subst_replace(data: str) -> str:

    for key, value in subst.items():
        data = data.replace(key, value)
    return data


if __name__ == "__main__":

    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--vendor",
        type=str,
        help="Vendor name",
        required=True,
    )
    parser.add_argument(
        "--example",
        type=str,
        help="Plugin basename",
        required=True,
    )
    parser.add_argument("--parent", type=str, default="Usb", help="Device parent GType")
    parser.add_argument(
        "--year", type=int, default=datetime.date.today().year, help="Copyright year"
    )
    parser.add_argument("--author", type=str, help="Copyright author", required=True)
    parser.add_argument("--email", type=str, help="Copyright email", required=True)
    args = parser.parse_args()

    try:
        _subst_add_string("Vendor", _fix_case(args.vendor))
        _subst_add_string("Example", _fix_case(args.example))
        _subst_add_string("Parent", args.parent)
        _subst_add_string("Year", str(args.year))
        _subst_add_string("Author", args.author)
        _subst_add_string("Email", args.email)
    except NotImplementedError as e:
        print(e)
        sys.exit(1)

    template_src = "vendor-example"
    os.makedirs(os.path.join("plugins", _subst_replace(template_src)), exist_ok=True)

    env = Environment(
        loader=FileSystemLoader("."),
        autoescape=select_autoescape(),
        keep_trailing_newline=True,
    )
    for fn in glob.iglob("./plugins/{}/*".format(template_src)):
        template = env.get_template(fn)
        with open(_subst_replace(fn.replace(".in", "")), "wb") as f_dst:
            f_dst.write(template.render(subst).encode())
