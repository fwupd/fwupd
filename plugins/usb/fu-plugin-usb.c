/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016-2017 Richard Hughes <richard@hughsie.com>
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

#include "fu-plugin.h"
#include "fu-plugin-vfuncs.h"

gboolean
fu_plugin_usb_device_added (FuPlugin *plugin, GUsbDevice *usb_device, GError **error)
{
	const gchar *platform_id = NULL;
	guint8 idx = 0x00;
	g_autofree gchar *product = NULL;
	g_autoptr(FuDevice) dev = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* ignore hubs */
	if (g_usb_device_get_device_class (device) == G_USB_DEVICE_CLASS_HUB)
		return TRUE;

	/* try to get the version without claiming interface */
	locker = fu_device_locker_new (device, error);
	if (locker == NULL)
		return FALSE;

	/* get version number, falling back to the USB device release */
	dev = fu_usb_device_new (device);
	idx = g_usb_device_get_custom_index (device,
					     G_USB_DEVICE_CLASS_VENDOR_SPECIFIC,
					     'F', 'W', NULL);
	if (idx != 0x00) {
		g_autofree gchar *version = NULL;
		version = g_usb_device_get_string_descriptor (device, idx, NULL);
		fu_device_set_version (dev, version);
	}

	/* get GUID from the descriptor if set */
	idx = g_usb_device_get_custom_index (device,
					     G_USB_DEVICE_CLASS_VENDOR_SPECIFIC,
					     'G', 'U', NULL);
	if (idx != 0x00) {
		g_autofree gchar *guid = NULL;
		guid = g_usb_device_get_string_descriptor (device, idx, NULL);
		fu_device_add_guid (dev, guid);
	}

	/* use a small delay for hotplugging so that other, better, plugins
	 * can claim this interface and add the FuDevice */
	fu_plugin_device_add_delay (plugin, dev);
	return TRUE;
}
