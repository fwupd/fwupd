/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-plugin-vfuncs.h"

#include "fu-synaptics-rmi-device.h"

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_plugin_add_rule (plugin, FU_PLUGIN_RULE_REQUIRES_QUIRK, FU_QUIRKS_PLUGIN);
	fu_plugin_add_rule (plugin, FU_PLUGIN_RULE_SUPPORTS_PROTOCOL, "com.synaptics.rmi");
	fu_plugin_add_udev_subsystem (plugin, "hidraw");
}

gboolean
fu_plugin_udev_device_added (FuPlugin *plugin, FuUdevDevice *device, GError **error)
{
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(FuSynapticsRmiDevice) dev = NULL;

	/* interesting device? */
	if (g_strcmp0 (fu_udev_device_get_subsystem (device), "hidraw") != 0)
		return TRUE;
	if (g_udev_device_get_device_file (fu_udev_device_get_dev (device)) == NULL)
		return TRUE;

	/* RMI4 device */
	dev = fu_synaptics_rmi_device_new (device);
	locker = fu_device_locker_new (dev, error);
	if (locker == NULL)
		return FALSE;
	fu_plugin_device_add (plugin, FU_DEVICE (dev));
	return TRUE;
}
