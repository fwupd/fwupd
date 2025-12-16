/*
 * Copyright 2025 Shihwei Huang <shihwei.huang@focaltech-electronics.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-focaltouch-firmware.h"
#include "fu-focaltouch-hid-device.h"
#include "fu-focaltouch-plugin.h"

struct _FuFocaltouchPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuFocaltouchPlugin, fu_focaltouch_plugin, FU_TYPE_PLUGIN)

static void
fu_focaltouch_plugin_init(FuFocaltouchPlugin *self)
{
}

static void
fu_focaltouch_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_udev_subsystem(plugin, "hidraw");

	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_FOCALTOUCH_FIRMWARE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_FOCALTOUCH_HID_DEVICE);
}

static void
fu_focaltouch_plugin_class_init(FuFocaltouchPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_focaltouch_plugin_constructed;
}
