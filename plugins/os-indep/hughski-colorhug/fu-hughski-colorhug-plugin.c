/*
 * Copyright 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-hughski-colorhug-device.h"
#include "fu-hughski-colorhug-plugin.h"

struct _FuHughskiColorhugPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuHughskiColorhugPlugin, fu_hughski_colorhug_plugin, FU_TYPE_PLUGIN)

static void
fu_hughski_colorhug_plugin_init(FuHughskiColorhugPlugin *self)
{
}

static void
fu_hughski_colorhug_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_HUGHSKI_COLORHUG_DEVICE);
}

static void
fu_hughski_colorhug_plugin_class_init(FuHughskiColorhugPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_hughski_colorhug_plugin_constructed;
}
