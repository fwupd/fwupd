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
	fu_plugin_add_rule (plugin, FU_PLUGIN_RULE_SUPPORTS_PROTOCOL, "com.8bitdo");
	fu_plugin_set_device_gtype (plugin, FU_TYPE_EBITDO_DEVICE);
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
