/*
 * Copyright (c) 1999-2023 Logitech, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-logitech-tap-hdmi-device.h"
#include "fu-logitech-tap-sensor-device.h"
#include "fu-logitech-tap-plugin.h"

struct _FuLogitechTapPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuLogitechTapPlugin, fu_logitech_tap_plugin, FU_TYPE_PLUGIN)

static void
fu_logitech_tap_plugin_init(FuLogitechTapPlugin *self)
{
}

static void
fu_logitech_tap_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_udev_subsystem(plugin, "video4linux");
	fu_plugin_add_udev_subsystem(plugin, "hidraw");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_LOGITECH_TAP_HDMI_DEVICE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_LOGITECH_TAP_SENSOR_DEVICE);
}

static void
fu_logitech_tap_plugin_class_init(FuLogitechTapPluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->constructed = fu_logitech_tap_plugin_constructed;
}
