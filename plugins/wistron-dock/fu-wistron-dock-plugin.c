/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-wistron-dock-device.h"
#include "fu-wistron-dock-plugin.h"

struct _FuWistronDockPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuWistronDockPlugin, fu_wistron_dock_plugin, FU_TYPE_PLUGIN)

static void
fu_wistron_dock_plugin_init(FuWistronDockPlugin *self)
{
}

static void
fu_wistron_dock_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_WISTRON_DOCK_DEVICE);
}

static void
fu_wistron_dock_plugin_class_init(FuWistronDockPluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->constructed = fu_wistron_dock_plugin_constructed;
}
