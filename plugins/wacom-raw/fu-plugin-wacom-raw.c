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
	fu_plugin_add_udev_subsystem (plugin, "hidraw");
}

gboolean
fu_plugin_update_detach (FuPlugin *plugin, FuDevice *device, GError **error)
{
	g_autoptr(FuDeviceLocker) locker = NULL;
	if (fu_device_has_flag (device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER))
		return TRUE;
	locker = fu_device_locker_new (device, error);
	if (locker == NULL)
		return FALSE;
	return fu_device_detach (device, error);
}

gboolean
fu_plugin_update_attach (FuPlugin *plugin, FuDevice *device, GError **error)
{
	g_autoptr(FuDeviceLocker) locker = fu_device_locker_new (device, error);
	if (locker == NULL)
		return FALSE;
	return fu_device_attach (device, error);
}

gboolean
fu_plugin_udev_device_added (FuPlugin *plugin, FuUdevDevice *device, GError **error)
{
	/* interesting device? */
	if (g_strcmp0 (fu_udev_device_get_subsystem (device), "hidraw") != 0)
		return TRUE;

	/* wacom */
	if (fu_udev_device_get_vendor (device) != FU_WACOM_DEVICE_VID)
		return TRUE;

	/* no actual device to open */
	if (g_udev_device_get_device_file (fu_udev_device_get_dev (device)) == NULL)
		return TRUE;

	/* EMR */
	if (fu_device_has_guid (FU_DEVICE (device), "WacomEMR")) {
		g_autoptr(FuWacomEmrDevice) dev = fu_wacom_emr_device_new (device);
		g_autoptr(FuDeviceLocker) locker = fu_device_locker_new (dev, error);
		if (locker == NULL)
			return FALSE;
		fu_plugin_device_add (plugin, FU_DEVICE (dev));
	}

	/* AES */
	if (fu_device_has_guid (FU_DEVICE (device), "WacomAES")) {
		g_autoptr(FuWacomAesDevice) dev = fu_wacom_aes_device_new (device);
		g_autoptr(FuDeviceLocker) locker = fu_device_locker_new (dev, error);
		if (locker == NULL)
			return FALSE;
		fu_plugin_device_add (plugin, FU_DEVICE (dev));
	}

	/* not supported */
	g_set_error_literal (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "Only EMR or AES devices are supported");
	return FALSE;
}

gboolean
fu_plugin_update (FuPlugin *plugin,
		  FuDevice *device,
		  GBytes *blob_fw,
		  FwupdInstallFlags flags,
		  GError **error)
{
	g_autoptr(FuDeviceLocker) locker = NULL;
	locker = fu_device_locker_new (device, error);
	if (locker == NULL)
		return FALSE;
	return fu_device_write_firmware (device, blob_fw, error);
}
