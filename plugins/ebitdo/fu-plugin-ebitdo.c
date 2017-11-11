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

static gboolean
fu_plugin_ebitdo_device_added (FuPlugin *plugin,
				 GUsbDevice *usb_device,
				 GError **error)
{
	FuEbitdoDeviceKind ebitdo_kind;
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(FuEbitdoDevice) dev = NULL;

	/* ignore wrong hardware */
	ebitdo_kind = fu_ebitdo_device_kind_from_dev (usb_device);
	if (ebitdo_kind == FU_EBITDO_DEVICE_KIND_UNKNOWN)
		return TRUE;

	/* open the device */
	dev = fu_ebitdo_device_new (ebitdo_kind, usb_device);
	locker = fu_device_locker_new (dev, error);
	if (locker == NULL)
		return FALSE;

	/* insert to hash */
	fu_plugin_device_add (plugin, FU_DEVICE (dev));
	fu_plugin_cache_add (plugin, g_usb_device_get_platform_id (usb_device), dev);
	return TRUE;
}

static void
ebitdo_write_progress_cb (goffset current, goffset total, gpointer user_data)
{
	FuPlugin *plugin = FU_PLUGIN (user_data);
	gdouble percentage = -1.f;
	if (total > 0)
		percentage = (100.f * (gdouble) current) / (gdouble) total;
	g_debug ("written %" G_GOFFSET_FORMAT "/%" G_GOFFSET_FORMAT " bytes [%.1f%%]",
		 current, total, percentage);
	fu_plugin_set_percentage (plugin, (guint) percentage);
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
	fu_plugin_set_status (plugin, FWUPD_STATUS_DEVICE_WRITE);
	if (!fu_ebitdo_device_write_firmware (ebitdo_dev, blob_fw,
					      ebitdo_write_progress_cb, plugin,
					      error))
		return FALSE;

	/* when doing a soft-reboot the device does not re-enumerate properly
	 * so manually reboot the GUsbDevice */
	fu_plugin_set_status (plugin, FWUPD_STATUS_DEVICE_RESTART);
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

static void
fu_plugin_ebitdo_device_added_cb (GUsbContext *ctx,
				    GUsbDevice *usb_device,
				    FuPlugin *plugin)
{
	g_autoptr(GError) error = NULL;
	if (!fu_plugin_ebitdo_device_added (plugin, usb_device, &error)) {
		if (!g_error_matches (error,
				      FWUPD_ERROR,
				      FWUPD_ERROR_NOT_SUPPORTED)) {
			g_warning ("Failed to add 8Bitdo device: %s",
				   error->message);
		}
	}
}

static void
fu_plugin_ebitdo_device_removed_cb (GUsbContext *ctx,
				    GUsbDevice *usb_device,
				    FuPlugin *plugin)
{
	FuDevice *dev;
	const gchar *platform_id = NULL;

	/* already in database */
	platform_id = g_usb_device_get_platform_id (usb_device);
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
			  G_CALLBACK (fu_plugin_ebitdo_device_added_cb),
			  plugin);
	g_signal_connect (usb_ctx, "device-removed",
			  G_CALLBACK (fu_plugin_ebitdo_device_removed_cb),
			  plugin);
	return TRUE;
}
