/*
 * Copyright 2024 Mario Limonciello <superm1@gmail.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-legion-hid2-device.h"
#include "fu-legion-hid2-firmware.h"
#include "fu-legion-hid2-plugin.h"

struct _FuLegionHid2Plugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuLegionHid2Plugin, fu_legion_hid2_plugin, FU_TYPE_PLUGIN)

static void
fu_legion_hid2_plugin_init(FuLegionHid2Plugin *self)
{
}

static void
fu_legion_hid2_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_LEGION_HID2_DEVICE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_LEGION_HID2_FIRMWARE);
}

static void
fu_legion_hid2_plugin_class_init(FuLegionHid2PluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_legion_hid2_plugin_constructed;
}
