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

#include "fu-device-ebitdo.h"

#include "fu-plugin.h"
#include "fu-plugin-vfuncs.h"

static gboolean
fu_plugin_device_ebitdo_added (FuPlugin *plugin,
				 GUsbDevice *usb_device,
				 GError **error)
{
	FuDeviceEbitdoKind ebitdo_kind;
	const gchar *platform_id = NULL;
	g_autofree gchar *runtime_id = NULL;
	g_autoptr(AsProfile) profile = as_profile_new ();
	g_autoptr(AsProfileTask) ptask = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(FuDeviceEbitdo) dev = NULL;

	/* ignore hubs */
	ptask = as_profile_start (profile, "FuPluginEbitdo:added{%04x:%04x}",
				  g_usb_device_get_vid (usb_device),
				  g_usb_device_get_pid (usb_device));
	g_assert (ptask != NULL);

	/* create the device */
	platform_id = g_usb_device_get_platform_id (usb_device);
	dev = fu_device_ebitdo_new (usb_device);
	if (dev == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "invalid 8Bitdo device type detected");
		return FALSE;
	}
	fu_device_set_id (dev, platform_id);

	/* add a hardcoded icon name */
	fu_device_add_icon (dev, "input-gaming");

	/* open the device */
	locker = fu_device_locker_new_full (dev,
					    (FuDeviceLockerFunc) fu_device_ebitdo_open,
					    (FuDeviceLockerFunc) fu_device_ebitdo_close,
					    error);
	if (locker == NULL)
		return FALSE;
	ebitdo_kind = fu_device_ebitdo_get_kind (dev);

	/* only the bootloader can do the update */
	runtime_id = g_strdup_printf ("%s-runtime", platform_id);
	if (ebitdo_kind == FU_DEVICE_EBITDO_KIND_BOOTLOADER) {
		FuDeviceEbitdo *dev_runtime;

		/* add the last seen runtime GUID too */
		dev_runtime = fu_plugin_cache_lookup (plugin, runtime_id);
		if (dev_runtime != NULL) {
			const gchar *guid = fu_device_get_guid_default (FU_DEVICE (dev_runtime));
			g_debug ("adding runtime GUID of %s", guid);
			fu_device_add_guid (FU_DEVICE (dev), guid);
		}
	} else {
		fu_plugin_cache_add (plugin, runtime_id, dev);
	}

	/* insert to hash */
	fu_plugin_device_add (plugin, FU_DEVICE (dev));
	fu_plugin_cache_add (plugin, platform_id, dev);
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
	GUsbContext *usb_ctx = fu_plugin_get_usb_context (plugin);
	FuDeviceEbitdo *ebitdo_dev = FU_DEVICE_EBITDO (dev);
	const gchar *platform_id;
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(GUsbDevice) usb_device = NULL;
	g_autoptr(GUsbDevice) usb_device2 = NULL;

	/* get version */
	platform_id = fu_device_get_id (dev);
	usb_device = g_usb_context_find_by_platform_id (usb_ctx,
							platform_id,
							error);
	if (usb_device == NULL)
		return FALSE;
	if (fu_device_ebitdo_get_kind (ebitdo_dev) != FU_DEVICE_EBITDO_KIND_BOOTLOADER) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "invalid 8Bitdo device type detected");
		return FALSE;
	}

	/* write the firmware */
	locker = fu_device_locker_new_full (ebitdo_dev,
					    (FuDeviceLockerFunc) fu_device_ebitdo_open,
					    (FuDeviceLockerFunc) fu_device_ebitdo_close,
					    error);
	if (locker == NULL)
		return FALSE;
	fu_plugin_set_status (plugin, FWUPD_STATUS_DEVICE_WRITE);
	if (!fu_device_ebitdo_write_firmware (ebitdo_dev, blob_fw,
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
	if (!fu_device_ebitdo_set_usb_device (ebitdo_dev, usb_device2, error)) {
		g_prefix_error (error, "wrong device came back: ");
		return FALSE;
	}

	/* get the new version number */
	locker = fu_device_locker_new_full (ebitdo_dev,
					    (FuDeviceLockerFunc) fu_device_ebitdo_open,
					    (FuDeviceLockerFunc) fu_device_ebitdo_close,
					    error);
	if (locker == NULL) {
		g_prefix_error (error, "failed to re-open device: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_plugin_device_ebitdo_added_cb (GUsbContext *ctx,
				    GUsbDevice *usb_device,
				    FuPlugin *plugin)
{
	g_autoptr(GError) error = NULL;
	if (!fu_plugin_device_ebitdo_added (plugin, usb_device, &error)) {
		if (!g_error_matches (error,
				      FWUPD_ERROR,
				      FWUPD_ERROR_NOT_SUPPORTED)) {
			g_warning ("Failed to add 8Bitdo device: %s",
				   error->message);
		}
	}
}

static void
fu_plugin_device_ebitdo_removed_cb (GUsbContext *ctx,
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
			  G_CALLBACK (fu_plugin_device_ebitdo_added_cb),
			  plugin);
	g_signal_connect (usb_ctx, "device-removed",
			  G_CALLBACK (fu_plugin_device_ebitdo_removed_cb),
			  plugin);
	return TRUE;
}
