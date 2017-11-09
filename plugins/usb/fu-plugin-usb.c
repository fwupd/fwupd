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

#include "fu-plugin.h"
#include "fu-plugin-vfuncs.h"

static gchar *
_bcd_version_from_uint16 (guint16 val)
{
#if AS_CHECK_VERSION(0,7,3)
	return as_utils_version_from_uint16 (val, AS_VERSION_PARSE_FLAG_USE_BCD);
#else
	guint maj = ((val >> 12) & 0x0f) * 10 + ((val >> 8) & 0x0f);
	guint min = ((val >> 4) & 0x0f) * 10 + (val & 0x0f);
	return g_strdup_printf ("%u.%u", maj, min);
#endif
}

static void
fu_plugin_usb_device_added_cb (GUsbContext *ctx,
				 GUsbDevice *device,
				 FuPlugin *plugin)
{
	const gchar *platform_id = NULL;
	guint8 idx = 0x00;
	g_autofree gchar *devid1 = NULL;
	g_autofree gchar *devid2 = NULL;
	g_autofree gchar *product = NULL;
	g_autofree gchar *vendor_id = NULL;
	g_autofree gchar *version = NULL;
	g_autoptr(AsProfile) profile = as_profile_new ();
	g_autoptr(AsProfileTask) ptask = NULL;
	g_autoptr(FuDevice) dev = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(GError) error = NULL;

	/* ignore hubs */
	if (g_usb_device_get_device_class (device) == G_USB_DEVICE_CLASS_HUB)
		return;
	ptask = as_profile_start (profile, "FuPluginUsb:added{%04x:%04x}",
				  g_usb_device_get_vid (device),
				  g_usb_device_get_pid (device));
	g_assert (ptask != NULL);

	/* is already in database */
	platform_id = g_usb_device_get_platform_id (device);
	dev = fu_plugin_cache_lookup (plugin, platform_id);
	if (dev != NULL) {
		g_debug ("ignoring duplicate %s", platform_id);
		return;
	}

	/* try to get the version without claiming interface */
	locker = fu_device_locker_new (device, &error);
	if (locker == NULL) {
		g_debug ("Failed to open: %s", error->message);
		return;
	}

	/* insert to hash if valid */
	dev = fu_device_new ();
	fu_device_set_id (dev, platform_id);

	/* get product */
	idx = g_usb_device_get_product_index (device);
	if (idx != 0x00) {
		g_autoptr(AsProfileTask) ptask2 = NULL;
		ptask2 = as_profile_start_literal (profile, "FuPluginUsb:get-string-desc");
		g_assert (ptask2 != NULL);
		product = g_usb_device_get_string_descriptor (device, idx, NULL);
	}
	if (product == NULL) {
		g_debug ("no product string descriptor");
		return;
	}
	fu_device_set_name (dev, product);

	/* get version number, falling back to the USB device release */
	idx = g_usb_device_get_custom_index (device,
					     G_USB_DEVICE_CLASS_VENDOR_SPECIFIC,
					     'F', 'W', NULL);
	if (idx != 0x00)
		version = g_usb_device_get_string_descriptor (device, idx, NULL);
	if (version == NULL) {
		guint16 release = g_usb_device_get_release (device);
		version = _bcd_version_from_uint16 (release);
	}
	fu_device_set_version (dev, version);

	/* set vendor ID */
	vendor_id = g_strdup_printf ("USB:0x%04X", g_usb_device_get_vid (device));
	fu_device_set_vendor_id (dev, vendor_id);

	/* get GUID from the descriptor if set */
	idx = g_usb_device_get_custom_index (device,
					     G_USB_DEVICE_CLASS_VENDOR_SPECIFIC,
					     'G', 'U', NULL);
	if (idx != 0x00) {
		g_autofree gchar *guid = NULL;
		guid = g_usb_device_get_string_descriptor (device, idx, NULL);
		fu_device_add_guid (dev, guid);
	}

	/* also fall back to the USB VID:PID hash */
	devid1 = g_strdup_printf ("USB\\VID_%04X&PID_%04X",
				  g_usb_device_get_vid (device),
				  g_usb_device_get_pid (device));
	fu_device_add_guid (dev, devid1);
	devid2 = g_strdup_printf ("USB\\VID_%04X&PID_%04X&REV_%04X",
				  g_usb_device_get_vid (device),
				  g_usb_device_get_pid (device),
				  g_usb_device_get_release (device));
	fu_device_add_guid (dev, devid2);

	/* use a small delay for hotplugging so that other, better, plugins
	 * can claim this interface and add the FuDevice */
	fu_plugin_device_add_delay (plugin, dev);

	/* insert to hash */
	fu_plugin_cache_add (plugin, platform_id, dev);
}

static void
fu_plugin_usb_device_removed_cb (GUsbContext *ctx,
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
			  G_CALLBACK (fu_plugin_usb_device_added_cb),
			  plugin);
	g_signal_connect (usb_ctx, "device-removed",
			  G_CALLBACK (fu_plugin_usb_device_removed_cb),
			  plugin);
	return TRUE;
}
