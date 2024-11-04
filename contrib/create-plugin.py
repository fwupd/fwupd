#!/usr/bin/env python3
#
# Copyright 2023 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# pylint: disable=invalid-name,missing-docstring,consider-using-f-string

import os
import datetime
import argparse
import glob
import sys
from jinja2 import Environment, FileSystemLoader, select_autoescape

subst = {}


# convert a snake-case name into CamelCase
def _to_camelcase(value: str) -> str:
    return "".join([tmp[0].upper() + tmp[1:].lower() for tmp in value.split("_")])


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
        vendor: str = args.vendor.replace("-", "_")
        example: str = args.example.replace("-", "_")

        # first in list
        subst["VendorExample"] = _to_camelcase(vendor) + _to_camelcase(example)
        subst["vendor_example"] = vendor.lower() + "_" + example.lower()
        subst["vendor_dash_example"] = vendor.lower() + "-" + example.lower()
        subst["VENDOR_EXAMPLE"] = vendor.upper() + "_" + example.upper()

        # second
        for key, value in {
            "Vendor": vendor,
            "Example": example,
            "Parent": args.parent,
            "Year": str(args.year),
            "Author": args.author,
            "Email": args.email,
        }.items():
            subst[key] = _to_camelcase(value)
            subst[key.lower()] = value.lower()
            subst[key.upper()] = value.upper()

    except NotImplementedError as e:
        print(e)
        sys.exit(1)

    template_src: str = "vendor-example"
    os.makedirs(os.path.join("plugins", _subst_replace(template_src)), exist_ok=True)

    srcdir: str = sys.argv[0].rsplit("/", maxsplit=2)[0]
    env = Environment(
        loader=FileSystemLoader(srcdir),
        autoescape=select_autoescape(),
        keep_trailing_newline=True,
    )
    for fn in glob.iglob(f"{srcdir}/plugins/{template_src}/**", recursive=True):
        if os.path.isdir(fn):
            fn_rel: str = os.path.relpath(fn, srcdir)
            os.makedirs(_subst_replace(fn_rel), exist_ok=True)
            continue
        fn_rel: str = os.path.relpath(fn, srcdir)
        template = env.get_template(fn_rel)
        filename: str = _subst_replace(fn_rel.replace(".in", ""))
        with open(filename, "wb") as f_dst:
            f_dst.write(template.render(subst).encode())
        print(f"wrote {filename}")
