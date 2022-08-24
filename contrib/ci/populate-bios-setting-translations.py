#!/usr/bin/python3
#
# Helper script to generate a list of translations
# Sample call:
# ./contrib/ci/populate-bios-attr-translations.py ./libfwupdplugin/tests/bios-attrs/dell-xps13-9310/
# will lead to ./libfwupdplugin/tests/bios-attrs/dell-xps13-9310/strings.txt
# which can be added to po/POTFILES.in
#
# Copyright (C) 2022  Mario Limonciello <superm1@gmail.com>
#
# SPDX-License-Identifier: LGPL-2.1+

import os
import sys


def populate_translations(path):
    output = open(os.path.join(path, "strings.txt"), "w")
    for root, _, files in os.walk(path):
        for file in files:
            val: str = ""
            if not file.endswith("display_name"):
                continue
            with open(os.path.join(root, file), "r") as f:
                val = f.read().replace('"', "").strip()
            if not val:
                continue
            output.write("#TRANSLATORS: Description of BIOS setting\n")
            output.write("%s\n\n" % val)
    output.close()


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("path to bios settings directory required")
        sys.exit(1)
    populate_translations(sys.argv[1])
