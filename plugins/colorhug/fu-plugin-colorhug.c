/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015-2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-plugin.h"
#include "fu-plugin-vfuncs.h"

#include "fu-colorhug-device.h"

gboolean
fu_plugin_update_detach (FuPlugin *plugin, FuDevice *device, GError **error)
{
	FuColorhugDevice *self = FU_COLORHUG_DEVICE (device);
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (device));
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(GUsbDevice) usb_device2 = NULL;

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
	g_clear_object (&locker);
	usb_device2 = g_usb_context_wait_for_replug (fu_plugin_get_usb_context (plugin),
						     usb_device,
						     10000, error);
	if (usb_device2 == NULL) {
		g_prefix_error (error, "device did not come back: ");
		return FALSE;
	}

	/* set the new device until we can use a new FuDevice */
	fu_usb_device_set_dev (FU_USB_DEVICE (self), usb_device2);

	/* success */
	return TRUE;
}

gboolean
fu_plugin_update_attach (FuPlugin *plugin, FuDevice *device, GError **error)
{
	FuColorhugDevice *self = FU_COLORHUG_DEVICE (device);
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (device));
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(GUsbDevice) usb_device2 = NULL;

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
	g_clear_object (&locker);
	usb_device2 = g_usb_context_wait_for_replug (fu_plugin_get_usb_context (plugin),
						     usb_device,
						     10000, error);
	if (usb_device2 == NULL) {
		g_prefix_error (error, "device did not come back: ");
		return FALSE;
	}

	/* set the new device until we can use a new FuDevice */
	fu_usb_device_set_dev (FU_USB_DEVICE (self), usb_device2);

	/* success */
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
	return fu_device_write_firmware (device, blob_fw, error);
}

gboolean
fu_plugin_usb_device_added (FuPlugin *plugin, GUsbDevice *usb_device, GError **error)
{
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(FuColorhugDevice) device = NULL;

	/* open the device */
	device = fu_colorhug_device_new (usb_device);
	fu_device_set_quirks (FU_DEVICE (device), fu_plugin_get_quirks (plugin));
	locker = fu_device_locker_new (device, error);
	if (locker == NULL)
		return FALSE;

	/* insert to hash */
	fu_plugin_device_add (plugin, FU_DEVICE (device));
	return TRUE;
}
