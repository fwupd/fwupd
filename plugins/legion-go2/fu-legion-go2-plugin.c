/*
 * Copyright 2025 lazro <li@shzj.cc>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-legion-go2-device.h"
#include "fu-legion-go2-plugin.h"

struct _FuLegionGo2Plugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuLegionGo2Plugin, fu_legion_go2_plugin, FU_TYPE_PLUGIN)

static void
fu_legion_go2_plugin_init(FuLegionGo2Plugin *self)
{
}

static void
fu_legion_go2_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_udev_subsystem(plugin, "hidraw");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_LEGION_GO2_DEVICE);
}

static void
fu_legion_go2_plugin_class_init(FuLegionGo2PluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_legion_go2_plugin_constructed;
}
