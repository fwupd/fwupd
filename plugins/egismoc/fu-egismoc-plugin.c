/*
 * Copyright 2025 Jason Huang <jason.huang@egistec.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-egismoc-device.h"
#include "fu-egismoc-plugin.h"

struct _FuEgismocPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuEgismocPlugin, fu_egismoc_plugin, FU_TYPE_PLUGIN)

static void
fu_egismoc_plugin_init(FuEgismocPlugin *self)
{
}

static void
fu_egismoc_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_EGISMOC_DEVICE);
}

static void
fu_egismoc_plugin_class_init(FuEgismocPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_egismoc_plugin_constructed;
}
