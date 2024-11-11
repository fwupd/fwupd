/*
 * Copyright 2024 Mike Chang <Mike.chang@telink-semi.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-telink-dfu-archive.h"
#include "fu-telink-dfu-ble-device.h"
#include "fu-telink-dfu-hid-device.h"
#include "fu-telink-dfu-plugin.h"

struct _FuTelinkDfuPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuTelinkDfuPlugin, fu_telink_dfu_plugin, FU_TYPE_PLUGIN)

static void
fu_telink_dfu_plugin_init(FuTelinkDfuPlugin *self)
{
}

static void
fu_telink_dfu_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	FuContext *ctx = fu_plugin_get_context(plugin);
	fu_context_add_quirk_key(ctx, "TelinkHidToolVer");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_TELINK_DFU_HID_DEVICE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_TELINK_DFU_BLE_DEVICE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_TELINK_DFU_ARCHIVE);
	fu_plugin_add_udev_subsystem(plugin, "hidraw");
}

static void
fu_telink_dfu_plugin_class_init(FuTelinkDfuPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_telink_dfu_plugin_constructed;
}
