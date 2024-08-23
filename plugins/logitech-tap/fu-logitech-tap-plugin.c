/*
 * Copyright 1999-2023 Logitech, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-logitech-tap-hdmi-device.h"
#include "fu-logitech-tap-plugin.h"
#include "fu-logitech-tap-sensor-device.h"
#include "fu-logitech-tap-touch-device.h"

struct _FuLogitechTapPlugin {
	FuPlugin parent_instance;
	FuDevice *hdmi_device;	 /* ref */
	FuDevice *sensor_device; /* ref */
	FuDevice *touch_device;	 /* ref */
};

G_DEFINE_TYPE(FuLogitechTapPlugin, fu_logitech_tap_plugin, FU_TYPE_PLUGIN)

static gboolean
fu_logitech_tap_plugin_composite_cleanup(FuPlugin *plugin, GPtrArray *devices, GError **error)
{
	FuLogitechTapPlugin *self = FU_LOGITECH_TAP_PLUGIN(plugin);

	/* check if HDMI firmware successfully upgraded and signal for SENSOR to trigger composite
	 * reboot is set */
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *dev = g_ptr_array_index(devices, i);

		if ((g_strcmp0(fu_device_get_plugin(dev), "logitech_tap") == 0) &&
		    (FU_IS_LOGITECH_TAP_HDMI_DEVICE(dev)) &&
		    (fu_device_has_private_flag(
			dev,
			FU_LOGITECH_TAP_HDMI_DEVICE_FLAG_SENSOR_NEEDS_REBOOT)) &&
		    self->hdmi_device != NULL) {
			g_debug("device needs reboot");
			if (!fu_logitech_tap_sensor_device_reboot_device(
				FU_LOGITECH_TAP_SENSOR_DEVICE(fu_device_get_proxy(dev)),
				error))
				return FALSE;
			fu_device_add_flag(dev, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
			break;
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
	fu_plugin_add_device_gtype(plugin, FU_TYPE_LOGITECH_TAP_TOUCH_DEVICE);
}

static void
fu_logitech_tap_plugin_device_registered(FuPlugin *plugin, FuDevice *device)
{
	FuLogitechTapPlugin *self = FU_LOGITECH_TAP_PLUGIN(plugin);
	if (g_strcmp0(fu_device_get_plugin(device), "logitech_tap") != 0)
		return;
	if (FU_IS_LOGITECH_TAP_HDMI_DEVICE(device)) {
		g_set_object(&self->hdmi_device, device);
		if (self->sensor_device != NULL)
			fu_device_set_proxy(self->hdmi_device, self->sensor_device);
	}
	if (FU_IS_LOGITECH_TAP_SENSOR_DEVICE(device)) {
		g_set_object(&self->sensor_device, device);
		if (self->hdmi_device != NULL)
			fu_device_set_proxy(self->hdmi_device, self->sensor_device);
	}
}

static void
fu_logitech_tap_plugin_finalize(GObject *obj)
{
	FuLogitechTapPlugin *self = FU_LOGITECH_TAP_PLUGIN(obj);
	if (self->hdmi_device != NULL)
		g_object_unref(self->hdmi_device);
	if (self->sensor_device != NULL)
		g_object_unref(self->sensor_device);
	if (self->touch_device != NULL)
		g_object_unref(self->touch_device);
	G_OBJECT_CLASS(fu_logitech_tap_plugin_parent_class)->finalize(obj);
}

static void
fu_logitech_tap_plugin_class_init(FuLogitechTapPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->finalize = fu_logitech_tap_plugin_finalize;
	plugin_class->constructed = fu_logitech_tap_plugin_constructed;
	plugin_class->device_registered = fu_logitech_tap_plugin_device_registered;
	plugin_class->composite_cleanup = fu_logitech_tap_plugin_composite_cleanup;
}
