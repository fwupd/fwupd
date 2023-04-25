/*
 * Copyright (c) 1999-2023 Logitech, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-logitech-tap-common.h"
#include "fu-logitech-tap-hdmi-device.h"
#include "fu-logitech-tap-plugin.h"
#include "fu-logitech-tap-sensor-device.h"

struct _FuLogitechTapPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuLogitechTapPlugin, fu_logitech_tap_plugin, FU_TYPE_PLUGIN)

static gboolean
fu_logitech_tap_plugin_composite_cleanup(FuPlugin *plugin, GPtrArray *devices, GError **error)
{
	gboolean needs_reboot = FALSE;
	GPtrArray *plugin_devices = fu_plugin_get_devices(plugin);

	/* check if HDMI firmware successfully upgraded and signal for SENSOR to trigger composite
	 * reboot is set */
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *dev = g_ptr_array_index(devices, i);

		if ((g_strcmp0(fu_device_get_plugin(dev), "logitech_tap") == 0) &&
		    (fu_device_has_private_flag(dev, FU_LOGITECH_TAP_DEVICE_TYPE_HDMI)) &&
		    (fu_device_has_private_flag(dev,
						FU_LOGITECH_TAP_HDMI_DEVICE_FLAG_NEEDS_REBOOT))) {
			needs_reboot = TRUE;
			g_debug("device needs reboot");
			break;
		}
	}
	if (needs_reboot) {
		for (guint i = 0; i < plugin_devices->len; i++) {
			FuDevice *dev = g_ptr_array_index(plugin_devices, i);

			if (fu_device_has_private_flag(dev, FU_LOGITECH_TAP_DEVICE_TYPE_SENSOR)) {
				if (!fu_logitech_tap_sensor_device_reboot_device(dev, error))
					return FALSE;
				break;
			}
		}
	}
	return TRUE;
}

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
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_logitech_tap_plugin_constructed;
	plugin_class->composite_cleanup = fu_logitech_tap_plugin_composite_cleanup;
}
