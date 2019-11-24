/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-plugin-vfuncs.h"
#include "fu-hash.h"

#include "fu-jabra-device.h"

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_plugin_set_device_gtype (plugin, FU_TYPE_JABRA_DEVICE);
}

/* slightly weirdly, this takes us from appIDLE back into the actual
 * runtime mode where the device actually works */
gboolean
fu_plugin_update_cleanup (FuPlugin *plugin,
			  FwupdInstallFlags flags,
			  FuDevice *device,
			  GError **error)
{
	GUsbDevice *usb_device;
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(GError) error_local = NULL;

	/* check for a property on the *dfu* FuDevice, which is also why we
	 * can't just rely on using FuDevice->cleanup() */
	if (!fu_device_has_custom_flag (device, "attach-extra-reset"))
		return TRUE;
	locker = fu_device_locker_new (device, error);
	if (locker == NULL)
		return FALSE;
	g_debug ("performing extra reset into firmware mode");
	usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (device));
	if (!g_usb_device_reset (usb_device, &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "cannot reset USB device: %s [%i]",
			     error_local->message,
			     error_local->code);
		return FALSE;
	}

	/* wait for device to re-appear */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_RESTART);
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}
