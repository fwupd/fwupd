/*
 * Copyright 2026 Yuchao Li <liyc44@lenovo.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-lenovo-accessory-ble-device.h"
#include "fu-lenovo-accessory-hid-bootloader.h"
#include "fu-lenovo-accessory-hid-device.h"
#include "fu-lenovo-accessory-plugin.h"

struct _FuLenovoAccessoryPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuLenovoAccessoryPlugin, fu_lenovo_accessory_plugin, FU_TYPE_PLUGIN)

static void
fu_lenovo_accessory_plugin_init(FuLenovoAccessoryPlugin *self)
{
	fu_plugin_add_flag(FU_PLUGIN(self), FWUPD_PLUGIN_FLAG_MUTABLE_ENUMERATION);
}

static void
fu_lenovo_accessory_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_LENOVO_ACCESSORY_HID_DEVICE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_LENOVO_ACCESSORY_HID_BOOTLOADER);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_LENOVO_ACCESSORY_BLE_DEVICE);
	fu_plugin_add_udev_subsystem(plugin, "hidraw");
}

static void
fu_lenovo_accessory_plugin_class_init(FuLenovoAccessoryPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_lenovo_accessory_plugin_constructed;
}
