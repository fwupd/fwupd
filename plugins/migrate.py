#!/usr/bin/python3
#
# Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: LGPL-2.1+
#
# pylint: disable=invalid-name,missing-docstring,consider-using-f-string

import glob
import os
import sys
import subprocess


def _convert(plugin_namespace: str, inp: str) -> str:

    template_snake = plugin_namespace.replace("-", "_")
    template_camel = "".join(part.title() for part in plugin_namespace.split("-"))
    template_upper = template_snake.upper()

    return (
        inp.replace("xxx", template_snake)
        .replace(
            "fu_plugin_{}_".format(template_snake),
            "fu_{}_plugin_".format(template_snake),
        )
        .replace("plugin_class", "plugin_class")
        .replace("Xxx", template_camel)
        .replace("XXX", template_upper)
    )


templateh = """/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

G_DECLARE_FINAL_TYPE(FuXxxPlugin, fu_xxx_plugin, FU, XXX_PLUGIN, FuPlugin)
"""

templatec = """/*
 * Copyright (C) FIXMEFIXMEFIXMEFIXMEFIXME
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-xxx-device.h"
#include "fu-xxx-plugin.h"

struct _FuXxxPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuXxxPlugin, fu_xxx_plugin, FU_TYPE_PLUGIN)

static void
fu_xxx_plugin_init(FuXxxPlugin *self)
{
}

static void
fu_xxx_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	FuContext *ctx = fu_plugin_get_context(plugin);
}

static void
fu_xxx_finalize(GObject *obj)
{
	FuXxxPlugin *self = FU_XXX_PLUGIN(obj);
	G_OBJECT_CLASS(fu_xxx_plugin_parent_class)->finalize(obj);
}

static void
fu_xxx_plugin_class_init(FuXxxPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->constructed = fu_xxx_constructed;
	object_class->finalize = fu_xxx_finalize;
	plugin_class->startup = fu_xxx_plugin_startup;
}
"""

if len(sys.argv) > 1:
    plugins = sys.argv[1:]
else:
    plugins = sorted(glob.iglob("*"))

for dirname in plugins:

    basename = dirname.replace("/", "")
    if not os.path.isdir(basename):
        continue

    plugin_namespace = {
        "pixart-rf": "pxi",
        "synaptics-prometheus": "synaprom",
        "wacom-usb": "wac",
        "goodix-moc": "goodixmoc",
        "intel-gsc": "igsc",
    }.get(basename, basename)

    newfnc = os.path.join(basename, "fu-{}-plugin.c".format(plugin_namespace))
    newfnh = os.path.join(basename, "fu-{}-plugin.h".format(plugin_namespace))

    if not os.path.exists(newfnh):
        with open(newfnh, "w") as f:
            f.write(_convert(plugin_namespace, templateh))
    if not os.path.exists(newfnc):
        with open(newfnc, "w") as f:
            f.write(_convert(plugin_namespace, templatec))
        subprocess.run(
            [
                "geany",
                newfnc,
                os.path.join(basename, "fu-plugin-{}.c".format(plugin_namespace)),
            ]
        )
    else:
        with open(newfnc, "r") as f:
            data = f.read()
        with open(newfnc, "w") as f:
            f.write(_convert(plugin_namespace, data))
