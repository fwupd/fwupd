#!/usr/bin/python3
# pylint: disable=invalid-name,missing-docstring
#
# Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: LGPL-2.1+

import sys

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
        f.write(
            '#include "{srcdir}/plugins/{dirname}/fu-{name}-plugin.h"\n'.format(
                srcdir=sys.argv[2], dirname=dirname, name=name.replace("_", "-")
            )
        )

    # GTypes
    gtypes = ["fu_{}_plugin_get_type".format(name) for _, name in plugin_names]
    f.write(
        "GType (*fu_plugin_externals[])(void) = { %s };\n"
        % ", ".join(gtypes + ["NULL"])
    )

sys.exit(0)
