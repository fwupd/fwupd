#!/usr/bin/env python3
# pylint: disable=invalid-name,missing-docstring
#
# Copyright 2022 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: LGPL-2.1-or-later

import sys
import os

if len(sys.argv) < 3:
    print("not enough arguments")
    sys.exit(1)

with open(sys.argv[1], "w") as f:
    # empty argument is no plugins
    plugin_names = []
    if sys.argv[3]:
        for fullpath in sys.argv[3].split(","):
            parts = fullpath.split("/")
            name = parts[-1]
            if name.startswith("libfu_plugin_"):
                name = name[13:]
            if name.endswith(".a"):
                name = name[:-2]
            plugin_names.append((parts[-2], name))

    # includes
    for dirname, name in plugin_names:
        base = None
        for root, dirs, _ in os.walk(sys.argv[2]):
            for dirpath in dirs:
                if dirpath == dirname:
                    base = os.path.join(root, dirpath)
                    break
            if base:
                break
        f.write(f'#include "{base}/fu-{name.replace("_", "-")}-plugin.h"\n')

    # GTypes
    gtypes = [f"fu_{name}_plugin_get_type" for _, name in plugin_names]
    f.write(
        "GType (*fu_plugin_externals[])(void) = { %s };\n"
        % ", ".join(gtypes + ["NULL"])
    )

sys.exit(0)
