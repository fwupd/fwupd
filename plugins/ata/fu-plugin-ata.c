/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-plugin-vfuncs.h"

#include "fu-ata-device.h"

gboolean
fu_plugin_udev_device_added (FuPlugin *plugin, FuUdevDevice *device, GError **error)
{
	GUdevDevice *udev_device = fu_udev_device_get_dev (device);
	g_autoptr(FuAtaDevice) dev = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* interesting device? */
	if (udev_device == NULL)
		return TRUE;
	if (g_strcmp0 (g_udev_device_get_subsystem (udev_device), "block") != 0)
		return TRUE;
	if (g_strcmp0 (g_udev_device_get_devtype (udev_device), "disk") != 0)
		return TRUE;
	if (!g_udev_device_get_property_as_boolean (udev_device, "ID_ATA_SATA"))
		return TRUE;
	if (!g_udev_device_get_property_as_boolean (udev_device, "ID_ATA_DOWNLOAD_MICROCODE"))
		return TRUE;

	dev = fu_ata_device_new (device);
	locker = fu_device_locker_new (dev, error);
	if (locker == NULL)
		return FALSE;
	fu_plugin_device_add (plugin, FU_DEVICE (dev));
	return TRUE;
}

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_plugin_add_udev_subsystem (plugin, "block");
	fu_plugin_add_rule (plugin, FU_PLUGIN_RULE_SUPPORTS_PROTOCOL, "org.t13.ata");
}
