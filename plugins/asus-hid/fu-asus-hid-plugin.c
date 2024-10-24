/*
 * Copyright 2024 Mario Limonciello <superm1@gmail.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-asus-hid-device.h"
#include "fu-asus-hid-firmware.h"
#include "fu-asus-hid-plugin.h"

struct _FuAsusHidPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuAsusHidPlugin, fu_asus_hid_plugin, FU_TYPE_PLUGIN)

static void
fu_asus_hid_plugin_init(FuAsusHidPlugin *self)
{
}

static void
fu_asus_hid_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	FuContext *ctx = fu_plugin_get_context(plugin);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_ASUS_HID_DEVICE);
	fu_context_add_quirk_key(ctx, "AsusHidNumMcu");
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_ASUS_HID_FIRMWARE);
}

static void
fu_asus_hid_plugin_class_init(FuAsusHidPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_asus_hid_plugin_constructed;
}
