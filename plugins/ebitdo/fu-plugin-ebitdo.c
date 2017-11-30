/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include "fu-ebitdo-device.h"

#include "fu-plugin.h"
#include "fu-plugin-vfuncs.h"

gboolean
fu_plugin_usb_device_added (FuPlugin *plugin, GUsbDevice *usb_device, GError **error)
{
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(FuEbitdoDevice) device = NULL;

	/* open the device */
	device = fu_ebitdo_device_new (usb_device);
	locker = fu_device_locker_new (device, error);
	if (locker == NULL)
		return FALSE;

	/* success */
	fu_plugin_device_add (plugin, FU_DEVICE (device));
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
	g_autoptr(GUsbDevice) usb_device2 = NULL;

	/* get version */
	if (fu_ebitdo_device_get_kind (ebitdo_dev) != FU_EBITDO_DEVICE_KIND_BOOTLOADER) {
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
	fu_device_set_status (dev, FWUPD_STATUS_DEVICE_WRITE);
	if (!fu_ebitdo_device_write_firmware (ebitdo_dev, blob_fw, error))
		return FALSE;

	/* when doing a soft-reboot the device does not re-enumerate properly
	 * so manually reboot the GUsbDevice */
	fu_device_set_status (dev, FWUPD_STATUS_DEVICE_RESTART);
	if (!g_usb_device_reset (usb_device, error)) {
		g_prefix_error (error, "failed to force-reset device: ");
		return FALSE;
	}
	g_clear_object (&locker);
	usb_device2 = g_usb_context_wait_for_replug (fu_plugin_get_usb_context (plugin),
						     usb_device, 10000, error);
	if (usb_device2 == NULL) {
		g_prefix_error (error, "device did not come back: ");
		return FALSE;
	}
	fu_usb_device_set_dev (FU_USB_DEVICE (ebitdo_dev), usb_device2);

	/* success */
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
