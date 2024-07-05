#!/usr/bin/env python3
# pylint: disable=invalid-name,missing-docstring,consider-using-f-string
#
# Copyright 2023 Richard Hughes <richard@hughsie.com>
# Copyright 2023 Mario Limonciello <superm1@gmail.com>
#
# SPDX-License-Identifier: LGPL-2.1-or-later

import os
import sys
import argparse
from jinja2 import Environment, FileSystemLoader


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("-o", "--output")
    parser.add_argument("-m", "--man", action="append", nargs=1)
    args, argv = parser.parse_known_args()
    if len(argv) != 1:
        print(f"usage: {sys.argv[0]} IN_HTML [-o OUT_HTML]\n")
        sys.exit(1)

    # strip the suffix of all args
    man = []
    if args.man:
        for obj in args.man:
            man += [obj[0].strip('" ').split(".md")[0]]
    subst = {"man": man}
    env = Environment(
        loader=FileSystemLoader(os.path.dirname(argv[0])),
    )
    template = env.get_template(os.path.basename(argv[0]))
    out = template.render(subst)

    # success
    if args.output:
        with open(args.output, "wb") as f_out:
            f_out.write(out.encode())
    else:
        print(out)
