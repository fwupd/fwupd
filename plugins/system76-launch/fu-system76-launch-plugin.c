/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2021 Jeremy Soller <jeremy@system76.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-system76-launch-device.h"
#include "fu-system76-launch-plugin.h"

struct _FuSystem76LaunchPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuSystem76LaunchPlugin, fu_system76_launch_plugin, FU_TYPE_PLUGIN)

static void
fu_system76_launch_plugin_init(FuSystem76LaunchPlugin *self)
{
}

static void
fu_system76_launch_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_SYSTEM76_LAUNCH_DEVICE);
}

static void
fu_system76_launch_plugin_class_init(FuSystem76LaunchPluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->constructed = fu_system76_launch_plugin_constructed;
}
