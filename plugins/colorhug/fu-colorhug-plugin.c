/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-colorhug-device.h"
#include "fu-colorhug-plugin.h"

struct _FuColorhugPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuColorhugPlugin, fu_colorhug_plugin, FU_TYPE_PLUGIN)

static void
fu_colorhug_plugin_init(FuColorhugPlugin *self)
{
}

static void
fu_colorhug_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_COLORHUG_DEVICE);
}

static void
fu_colorhug_plugin_class_init(FuColorhugPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_colorhug_plugin_constructed;
}
