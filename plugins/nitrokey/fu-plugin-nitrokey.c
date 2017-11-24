/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
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

#include "fu-nitrokey-device.h"
#include "fu-nitrokey-common.h"

static void
fu_plugin_nitrokey_device_added_cb (GUsbContext *ctx,
				    GUsbDevice *usb_device,
				    FuPlugin *plugin)
{
	const gchar *platform_id = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(FuNitrokeyDevice) dev = NULL;
	g_autoptr(GError) error = NULL;

	/* not the right kind of device */
	if (g_usb_device_get_vid (usb_device) != 0x20a0)
		return;
	if (g_usb_device_get_pid (usb_device) != 0x4109)
		return;

	/* is already in database */
	platform_id = g_usb_device_get_platform_id (usb_device);
	dev = fu_plugin_cache_lookup (plugin, platform_id);
	if (dev != NULL) {
		g_debug ("ignoring duplicate %s", platform_id);
		return;
	}

	/* open the device */
	dev = fu_nitrokey_device_new (usb_device);
	locker = fu_device_locker_new (dev, &error);
	if (locker == NULL) {
		g_warning ("failed to open device: %s", error->message);
		return;
	}

	fu_plugin_device_add (plugin, FU_DEVICE (dev));
	fu_plugin_cache_add (plugin, platform_id, dev);
}

static void
fu_plugin_nitrokey_device_removed_cb (GUsbContext *ctx,
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
			  G_CALLBACK (fu_plugin_nitrokey_device_added_cb),
			  plugin);
	g_signal_connect (usb_ctx, "device-removed",
			  G_CALLBACK (fu_plugin_nitrokey_device_removed_cb),
			  plugin);
	return TRUE;
}
