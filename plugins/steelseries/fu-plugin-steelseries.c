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

#include <appstream-glib.h>
#include <string.h>

#include "fu-plugin.h"
#include "fu-plugin-vfuncs.h"

#define STEELSERIES_TRANSACTION_TIMEOUT		1000 /* ms */

static void
fu_plugin_steelseries_device_added_cb (GUsbContext *ctx,
				       GUsbDevice *usb_device,
				       FuPlugin *plugin)
{
	const gchar *platform_id = NULL;
	const guint8 iface_idx = 0x00;
	gboolean ret;
	gsize actual_len = 0;
	guint8 data[32];
	g_autofree gchar *devid1 = NULL;
	g_autofree gchar *version = NULL;
	g_autoptr(AsProfile) profile = as_profile_new ();
	g_autoptr(AsProfileTask) ptask = NULL;
	g_autoptr(FuDevice) dev = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(GError) error_local = NULL;

	/* not the right kind of device */
	if (g_usb_device_get_vid (usb_device) != 0x1038)
		return;
	if (g_usb_device_get_pid (usb_device) != 0x1702)
		return;

	/* profile */
	ptask = as_profile_start (profile, "FuPluginSteelseries:added{%04x:%04x}",
				  g_usb_device_get_vid (usb_device),
				  g_usb_device_get_pid (usb_device));
	g_assert (ptask != NULL);

	/* is already in database */
	platform_id = g_usb_device_get_platform_id (usb_device);
	dev = fu_plugin_cache_lookup (plugin, platform_id);
	if (dev != NULL) {
		g_debug ("ignoring duplicate %s", platform_id);
		return;
	}

	/* get exclusive access */
	locker = fu_device_locker_new (usb_device, &error_local);
	if (locker == NULL) {
		g_warning ("failed to open device: %s", error_local->message);
		return;
	}
	if (!g_usb_device_claim_interface (usb_device, iface_idx,
					   G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
					   &error_local)) {
		g_warning ("failed to claim interface: %s", error_local->message);
		return;
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
					     &error_local);
	if (!ret) {
		g_debug ("failed to do control transfer: %s", error_local->message);
		return;
	}
	if (actual_len != 32) {
		g_warning ("only wrote %" G_GSIZE_FORMAT "bytes", actual_len);
		return;
	}
	ret = g_usb_device_interrupt_transfer (usb_device,
					       0x81, /* EP1 IN */
					       data,
					       sizeof(data),
					       &actual_len,
					       STEELSERIES_TRANSACTION_TIMEOUT,
					       NULL,
					       &error_local);
	if (!ret) {
		g_debug ("failed to do EP1 transfer: %s", error_local->message);
		return;
	}
	if (actual_len != 32) {
		g_warning ("only read %" G_GSIZE_FORMAT "bytes", actual_len);
		return;
	}

	/* insert to hash if valid */
	dev = fu_device_new ();
	fu_device_set_id (dev, platform_id);
	fu_device_set_name (dev, "SteelSeries Rival 100");
	fu_device_set_vendor (dev, "SteelSeries");
	fu_device_set_summary (dev, "An optical gaming mouse");
	fu_device_add_icon (dev, "input-mouse");
	version = g_strdup_printf ("%i.%i.%i",
				   data[0], data[1], data[2]);
	fu_device_set_version (dev, version);

	/* use the USB VID:PID hash */
	devid1 = g_strdup_printf ("USB\\VID_%04X&PID_%04X",
				  g_usb_device_get_vid (usb_device),
				  g_usb_device_get_pid (usb_device));
	fu_device_add_guid (dev, devid1);

	/* we're done here */
	if (!g_usb_device_release_interface (usb_device, iface_idx,
					     G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
					     &error_local)) {
		g_warning ("failed to release interface: %s", error_local->message);
		return;
	}
	fu_plugin_device_add (plugin, dev);
	fu_plugin_cache_add (plugin, platform_id, dev);
}

static void
fu_plugin_steelseries_device_removed_cb (GUsbContext *ctx,
					 GUsbDevice *device,
					 FuPlugin *plugin)
{
	FuDevice *dev;
	const gchar *platform_id = NULL;

	/* already in database */
	platform_id = g_usb_device_get_platform_id (device);
	dev = fu_plugin_cache_lookup (plugin, platform_id);
	if (dev == NULL)
		return;

	fu_plugin_device_remove (plugin, dev);
	fu_plugin_cache_remove (plugin, platform_id);
}

gboolean
fu_plugin_startup (FuPlugin *plugin, GError **error)
{
	GUsbContext *usb_ctx = fu_plugin_get_usb_context (plugin);
	g_signal_connect (usb_ctx, "device-added",
			  G_CALLBACK (fu_plugin_steelseries_device_added_cb),
			  plugin);
	g_signal_connect (usb_ctx, "device-removed",
			  G_CALLBACK (fu_plugin_steelseries_device_removed_cb),
			  plugin);
	return TRUE;
}
