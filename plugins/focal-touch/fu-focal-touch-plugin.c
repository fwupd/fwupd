/*
 * Copyright 2025 Shihwei Huang <shihwei.huang@focaltech-electronics.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-focal-touch-firmware.h"
#include "fu-focal-touch-hid-device.h"
#include "fu-focal-touch-plugin.h"

struct _FuFocalTouchPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuFocalTouchPlugin, fu_focal_touch_plugin, FU_TYPE_PLUGIN)

static void
fu_focal_touch_plugin_init(FuFocalTouchPlugin *self)
{
}

static void
fu_focal_touch_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_udev_subsystem(plugin, "hidraw");

	fu_plugin_add_firmware_gtype(plugin, FU_TYPE_FOCAL_TOUCH_FIRMWARE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_FOCAL_TOUCH_HID_DEVICE);

	/* chain up to parent */
	G_OBJECT_CLASS(fu_focal_touch_plugin_parent_class)->constructed(obj);
}

static void
fu_focal_touch_plugin_class_init(FuFocalTouchPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_focal_touch_plugin_constructed;
}
