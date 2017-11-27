/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016-2017 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include "config.h"

#include <string.h>

#include "fu-steelseries-device.h"

#define STEELSERIES_TRANSACTION_TIMEOUT		1000 /* ms */

G_DEFINE_TYPE (FuSteelseriesDevice, fu_steelseries_device, FU_TYPE_USB_DEVICE)

static gboolean
fu_steelseries_device_open (FuUsbDevice *device, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (device);
	const guint8 iface_idx = 0x00;
	gboolean ret;
	gsize actual_len = 0;
	guint8 data[32];
	g_autofree gchar *version = NULL;

	/* hardcoded */
	fu_device_set_name (FU_DEVICE (device), "SteelSeries Rival 100");
	fu_device_set_vendor (FU_DEVICE (device), "SteelSeries");
	fu_device_set_summary (FU_DEVICE (device), "An optical gaming mouse");
	fu_device_add_icon (FU_DEVICE (device), "input-mouse");
	version = g_strdup_printf ("%i.%i.%i", data[0], data[1], data[2]);
	fu_device_set_version (FU_DEVICE (device), version);

	/* get firmware version on SteelSeries Rival 100 */
	if (!g_usb_device_claim_interface (usb_device, iface_idx,
					   G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
					   error)) {
		g_prefix_error (error, "failed to claim interface: ");
		return FALSE;
	}
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

	/* success */
	return TRUE;
}

static gboolean
fu_steelseries_device_close (FuUsbDevice *device, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (device);
	const guint8 iface_idx = 0x00;

	/* we're done here */
	if (!g_usb_device_release_interface (usb_device, iface_idx,
					     G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
					     error)) {
		g_prefix_error (error, "failed to release interface: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_steelseries_device_init (FuSteelseriesDevice *device)
{
}

static void
fu_steelseries_device_class_init (FuSteelseriesDeviceClass *klass)
{
	FuUsbDeviceClass *klass_usb_device = FU_USB_DEVICE_CLASS (klass);
	klass_usb_device->open = fu_steelseries_device_open;
	klass_usb_device->close = fu_steelseries_device_close;
}

FuSteelseriesDevice *
fu_steelseries_device_new (GUsbDevice *usb_device)
{
	FuSteelseriesDevice *device = NULL;
	device = g_object_new (FU_TYPE_STEELSERIES_DEVICE,
			       "usb-device", usb_device,
			       NULL);
	return device;
}
