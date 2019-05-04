/*
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-ebitdo-device.h"

#include "fu-plugin-vfuncs.h"

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_plugin_add_rule (plugin, FU_PLUGIN_RULE_REQUIRES_QUIRK, FU_QUIRKS_PLUGIN);
	fu_plugin_add_rule (plugin, FU_PLUGIN_RULE_SUPPORTS_PROTOCOL, "com.8bitdo");
}

gboolean
fu_plugin_usb_device_added (FuPlugin *plugin, FuUsbDevice *device, GError **error)
{
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(FuEbitdoDevice) dev = NULL;

	/* open the device */
	dev = fu_ebitdo_device_new (device);
	locker = fu_device_locker_new (dev, error);
	if (locker == NULL)
		return FALSE;

	/* success */
	fu_plugin_device_add (plugin, FU_DEVICE (dev));
	return TRUE;
}

gboolean
fu_plugin_update (FuPlugin *plugin,
		  FuDevice *dev,
		  GBytes *blob_fw,
		  FwupdInstallFlags flags,
		  GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (dev));
	FuEbitdoDevice *ebitdo_dev = FU_EBITDO_DEVICE (dev);
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* get version */
	if (!fu_device_has_flag (dev, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "invalid 8Bitdo device type detected");
		return FALSE;
	}

	/* write the firmware */
	locker = fu_device_locker_new (ebitdo_dev, error);
	if (locker == NULL)
		return FALSE;
	if (!fu_device_write_firmware (FU_DEVICE (ebitdo_dev), blob_fw, flags, error))
		return FALSE;

	/* when doing a soft-reboot the device does not re-enumerate properly
	 * so manually reboot the GUsbDevice */
	fu_device_set_status (dev, FWUPD_STATUS_DEVICE_RESTART);
	if (!g_usb_device_reset (usb_device, error)) {
		g_prefix_error (error, "failed to force-reset device: ");
		return FALSE;
	}

	/* wait for replug */
	fu_device_add_flag (dev, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

gboolean
fu_plugin_update_reload (FuPlugin *plugin, FuDevice *dev, GError **error)
{
	FuEbitdoDevice *ebitdo_dev = FU_EBITDO_DEVICE (dev);
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* get the new version number */
	locker = fu_device_locker_new (ebitdo_dev, error);
	if (locker == NULL) {
		g_prefix_error (error, "failed to re-open device: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}
