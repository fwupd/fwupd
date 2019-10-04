/*
 * Copyright (C) 2018-2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-plugin-vfuncs.h"
#include "fu-wacom-aes-device.h"
#include "fu-wacom-emr-device.h"
#include "fu-wacom-common.h"

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_plugin_add_rule (plugin, FU_PLUGIN_RULE_SUPPORTS_PROTOCOL, "com.wacom.raw");
	fu_plugin_add_udev_subsystem (plugin, "hidraw");
}

gboolean
fu_plugin_udev_device_added (FuPlugin *plugin, FuUdevDevice *device, GError **error)
{
	/* interesting device? */
	if (g_strcmp0 (fu_udev_device_get_subsystem (device), "hidraw") != 0)
		return TRUE;

	/* no actual device to open */
	if (g_udev_device_get_device_file (fu_udev_device_get_dev (device)) == NULL)
		return TRUE;

	/* EMR */
	if (fu_device_has_instance_id (FU_DEVICE (device), "WacomEMR")) {
		g_autoptr(FuWacomEmrDevice) dev = fu_wacom_emr_device_new (device);
		g_autoptr(FuDeviceLocker) locker = fu_device_locker_new (dev, error);
		if (locker == NULL)
			return FALSE;
		fu_plugin_device_add (plugin, FU_DEVICE (dev));
	}

	/* AES */
	if (fu_device_has_instance_id (FU_DEVICE (device), "WacomAES")) {
		g_autoptr(FuWacomAesDevice) dev = fu_wacom_aes_device_new (device);
		g_autoptr(FuDeviceLocker) locker = fu_device_locker_new (dev, error);
		if (locker == NULL)
			return FALSE;
		fu_plugin_device_add (plugin, FU_DEVICE (dev));
	}

	return TRUE;
}
