/*
 * Copyright (C) 2015-2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-plugin-vfuncs.h"

#include "fu-colorhug-device.h"

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_plugin_add_rule (plugin, FU_PLUGIN_RULE_REQUIRES_QUIRK, FU_QUIRKS_PLUGIN);
	fu_plugin_add_rule (plugin, FU_PLUGIN_RULE_SUPPORTS_PROTOCOL, "com.hughski.colorhug");
}

gboolean
fu_plugin_update_detach (FuPlugin *plugin, FuDevice *device, GError **error)
{
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* open device */
	locker = fu_device_locker_new (device, error);
	if (locker == NULL)
		return FALSE;

	/* switch to bootloader mode is not required */
	if (fu_device_has_flag (device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_debug ("already in bootloader mode, skipping");
		return TRUE;
	}

	/* reset */
	if (!fu_device_detach (FU_DEVICE (device), error))
		return FALSE;

	/* wait for replug */
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

gboolean
fu_plugin_update_attach (FuPlugin *plugin, FuDevice *device, GError **error)
{
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* open device */
	locker = fu_device_locker_new (device, error);
	if (locker == NULL)
		return FALSE;

	/* switch to runtime mode is not required */
	if (!fu_device_has_flag (device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_debug ("already in runtime mode, skipping");
		return TRUE;
	}

	/* reset */
	if (!fu_device_attach (device, error))
		return FALSE;

	/* wait for replug */
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

gboolean
fu_plugin_update_reload (FuPlugin *plugin, FuDevice *device, GError **error)
{
	FuColorhugDevice *self = FU_COLORHUG_DEVICE (device);
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* also set flash success */
	locker = fu_device_locker_new (device, error);
	if (locker == NULL)
		return FALSE;
	if (!fu_colorhug_device_set_flash_success (self, TRUE, error))
		return FALSE;
	return TRUE;
}

gboolean
fu_plugin_update (FuPlugin *plugin,
		  FuDevice *device,
		  GBytes *blob_fw,
		  FwupdInstallFlags flags,
		  GError **error)
{
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* write firmware */
	locker = fu_device_locker_new (device, error);
	if (locker == NULL)
		return FALSE;
	return fu_device_write_firmware (device, blob_fw, flags, error);
}

gboolean
fu_plugin_usb_device_added (FuPlugin *plugin, FuUsbDevice *device, GError **error)
{
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(FuColorhugDevice) dev = NULL;

	/* open the device */
	dev = fu_colorhug_device_new (device);
	locker = fu_device_locker_new (dev, error);
	if (locker == NULL)
		return FALSE;

	/* insert to hash */
	fu_plugin_device_add (plugin, FU_DEVICE (dev));
	return TRUE;
}
