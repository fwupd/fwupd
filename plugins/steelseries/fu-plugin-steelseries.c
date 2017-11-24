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

#include <string.h>

#include "fu-plugin.h"
#include "fu-plugin-vfuncs.h"

#define STEELSERIES_TRANSACTION_TIMEOUT		1000 /* ms */

gboolean
fu_plugin_usb_device_added (FuPlugin *plugin, GUsbDevice *usb_device, GError **error)
{
	const guint8 iface_idx = 0x00;
	gboolean ret;
	gsize actual_len = 0;
	guint8 data[32];
	g_autofree gchar *version = NULL;
	g_autoptr(FuDevice) dev = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(GError) error_local = NULL;

	/* not the right kind of device */
	if (g_usb_device_get_vid (usb_device) != 0x1038)
		return TRUE;
	if (g_usb_device_get_pid (usb_device) != 0x1702)
		return TRUE;

	/* get exclusive access */
	locker = fu_device_locker_new (usb_device, error);
	if (locker == NULL) {
		g_prefix_error (error, "failed to open device: ");
		return FALSE;
	}
	if (!g_usb_device_claim_interface (usb_device, iface_idx,
					   G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
					   error)) {
		g_prefix_error (error, "failed to claim interface: ");
		return FALSE;
	}

	/* get firmware version on SteelSeries Rival 100 */
	memset (data, 0x00, sizeof(data));
	data[0] = 0x16;
	ret = g_usb_device_control_transfer (usb_device,
					     G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					     G_USB_DEVICE_REQUEST_TYPE_CLASS,
					     G_USB_DEVICE_RECIPIENT_INTERFACE,
					     0x09,
					     0x0200,
					     0x0000,
					     data,
					     sizeof(data),
					     &actual_len,
					     STEELSERIES_TRANSACTION_TIMEOUT,
					     NULL,
					     error);
	if (!ret) {
		g_prefix_error (error, "failed to do control transfer: ");
		return FALSE;
	}
	if (actual_len != 32) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "only wrote %" G_GSIZE_FORMAT "bytes", actual_len);
		return FALSE;
	}
	ret = g_usb_device_interrupt_transfer (usb_device,
					       0x81, /* EP1 IN */
					       data,
					       sizeof(data),
					       &actual_len,
					       STEELSERIES_TRANSACTION_TIMEOUT,
					       NULL,
					       error);
	if (!ret) {
		g_prefix_error (error, "failed to do EP1 transfer: ");
		return FALSE;
	}
	if (actual_len != 32) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "only read %" G_GSIZE_FORMAT "bytes", actual_len);
		return FALSE;
	}

	/* insert to hash if valid */
	dev = fu_usb_device_new (usb_device);
	fu_device_set_name (dev, "SteelSeries Rival 100");
	fu_device_set_vendor (dev, "SteelSeries");
	fu_device_set_summary (dev, "An optical gaming mouse");
	fu_device_add_icon (dev, "input-mouse");
	version = g_strdup_printf ("%i.%i.%i",
				   data[0], data[1], data[2]);
	fu_device_set_version (dev, version);

	/* we're done here */
	if (!g_usb_device_release_interface (usb_device, iface_idx,
					     G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
					     error)) {
		g_prefix_error (error, "failed to release interface: ");
		return FALSE;
	}
	fu_plugin_device_add (plugin, dev);
	return TRUE;
}
