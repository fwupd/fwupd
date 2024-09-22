/*
 * Copyright 2024 Mario Limonciello <superm1@gmail.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-legion-hid-device.h"
#include "fu-legion-hid-plugin.h"

struct _FuLegionHidPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuLegionHidPlugin, fu_legion_hid_plugin, FU_TYPE_PLUGIN)

static void
fu_legion_hid_plugin_init(FuLegionHidPlugin *self)
{
}

static void
fu_legion_hid_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_LEGION_HID_DEVICE);
}

static void
fu_legion_hid_plugin_class_init(FuLegionHidPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_legion_hid_plugin_constructed;
}
