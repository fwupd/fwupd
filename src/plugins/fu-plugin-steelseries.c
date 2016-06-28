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

#include <gusb.h>
#include <string.h>

#include "fu-plugin.h"

#define STEELSERIES_REPLUG_TIMEOUT		5000 /* ms */
#define STEELSERIES_TRANSACTION_TIMEOUT		1000 /* ms */

/**
 * fu_plugin_get_name:
 */
const gchar *
fu_plugin_get_name (void)
{
	return "steelseries";
}

/**
 * fu_plugin_device_probe:
 **/
gboolean
fu_plugin_device_probe (FuPlugin *plugin, FuDevice *device, GError **error)
{
	GUsbDeviceClaimInterfaceFlags flags;
	const gchar *platform_id;
	const guint iface_idx = 0x00;
	gboolean ret;
	gsize actual_len = 0;
	guint8 data[32];
	g_autofree gchar *version = NULL;
	g_autoptr(GUsbContext) usb_ctx = NULL;
	g_autoptr(GUsbDevice) usb_device = NULL;

	/* get version */
	platform_id = fu_device_get_id (device);
	usb_ctx = g_usb_context_new (NULL);
	usb_device = g_usb_context_find_by_platform_id (usb_ctx,
							platform_id,
							error);
	if (usb_device == NULL)
		return FALSE;

	/* get exclusive access */
	if (!g_usb_device_open (usb_device, error))
		return FALSE;
	flags = G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER;
	if (!g_usb_device_claim_interface (usb_device, iface_idx, flags, error)) {
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
			     G_IO_ERROR_FAILED,
			     "only wrote %" G_GSIZE_FORMAT "bytes",
			     actual_len);
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
		g_prefix_error (error, "failed to do IN transfer: ");
		return FALSE;
	}
	if (actual_len != 32) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "only read %" G_GSIZE_FORMAT "bytes",
			     actual_len);
	}

	/* update */
	version = g_strdup_printf ("%i.%i.%i",
				   data[0], data[1], data[2]);
	fu_device_set_version (device, version);
	g_debug ("overriding the version with %s", version);

	/* FIXME: we can't do this until we know how to flash the firmware */
//	fu_device_add_flag (device, FU_DEVICE_FLAG_ALLOW_ONLINE);

	/* release device */
	if (!g_usb_device_release_interface (usb_device, iface_idx, flags, error)) {
		g_prefix_error (error, "failed to release interface: ");
		return FALSE;
	}
	if (!g_usb_device_close (usb_device, error))
		return FALSE;

	return TRUE;
}

/**
 * fu_plugin_device_update:
 **/
gboolean
fu_plugin_device_update (FuPlugin *plugin,
			 FuDevice *device,
			 GBytes *data,
			 GError **error)
{
	const gchar *platform_id;
	g_autoptr(GUsbContext) usb_ctx = NULL;
	g_autoptr(GUsbDevice) usb_device = NULL;
	g_autoptr(GUsbDevice) usb_devnew = NULL;

	/* get GUsbDevice */
	platform_id = fu_device_get_id (device);
	usb_ctx = g_usb_context_new (NULL);
	usb_device = g_usb_context_find_by_platform_id (usb_ctx,
							platform_id,
							error);
	if (usb_device == NULL)
		return FALSE;

	// if not bootloader
	//     issue vendor specific command and wait for replug
	usb_devnew = g_usb_context_wait_for_replug (usb_ctx,
						    usb_device,
						    STEELSERIES_REPLUG_TIMEOUT,
						    error);
	if (usb_devnew == NULL)
		return FALSE;

	/* open device */
	if (!g_usb_device_open (usb_devnew, error))
		return FALSE;

	// squirt in firmware

	/* close device */
	if (!g_usb_device_close (usb_devnew, error))
		return FALSE;
	return TRUE;
}
