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

#include <appstream-glib.h>

#include "fu-plugin.h"
#include "fu-plugin-vfuncs.h"

#include "fu-device-altos.h"

static gboolean
fu_plugin_altos_device_added (FuPlugin *plugin,
			      GUsbDevice *usb_device,
			      GError **error)
{
	const gchar *platform_id = NULL;
	g_autofree gchar *runtime_id = NULL;
	g_autoptr(AsProfile) profile = as_profile_new ();
	g_autoptr(AsProfileTask) ptask = NULL;
	g_autoptr(FuDeviceAltos) dev = NULL;

	/* profile */
	ptask = as_profile_start (profile, "FuPluginAltos:added{%04x:%04x}",
				  g_usb_device_get_vid (usb_device),
				  g_usb_device_get_pid (usb_device));
	g_assert (ptask != NULL);

	/* get kind */
	dev = fu_device_altos_new (usb_device);
	if (dev == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "not a ChaosKey device");
		return FALSE;
	}

	/* create the device */
	platform_id = g_usb_device_get_platform_id (usb_device);
	fu_device_set_id (FU_DEVICE (dev), platform_id);

	/* get device properties */
	if (!fu_device_altos_probe (dev, error))
		return FALSE;

	/* only the bootloader can do the update */
	runtime_id = g_strdup_printf ("%s-runtime", platform_id);
	if (fu_device_altos_get_kind (dev) == FU_DEVICE_ALTOS_KIND_BOOTLOADER) {
		FuDevice *dev_runtime;
		dev_runtime = fu_plugin_cache_lookup (plugin, runtime_id);
		if (dev_runtime != NULL) {
			const gchar *guid = fu_device_get_guid_default (dev_runtime);
			g_debug ("adding runtime GUID of %s", guid);
			fu_device_add_guid (FU_DEVICE (dev), guid);
			fu_device_set_version (FU_DEVICE (dev),
					       fu_device_get_version (dev_runtime));
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
fu_plugin_altos_progress_cb (goffset current, goffset total, gpointer user_data)
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
fu_plugin_verify (FuPlugin *plugin,
		  FuDevice *dev,
		  FuPluginVerifyFlags flags,
		  GError **error)
{
	g_autoptr(GBytes) blob_fw = NULL;
	GChecksumType checksum_types[] = {
		G_CHECKSUM_SHA1,
		G_CHECKSUM_SHA256,
		0 };

	/* get data */
	fu_plugin_set_status (plugin, FWUPD_STATUS_DEVICE_VERIFY);
	blob_fw = fu_device_altos_read_firmware (FU_DEVICE_ALTOS (dev),
						 fu_plugin_altos_progress_cb,
						 plugin,
						 error);
	if (blob_fw == NULL)
		return FALSE;
	for (guint i = 0; checksum_types[i] != 0; i++) {
		g_autofree gchar *hash = NULL;
		hash = g_compute_checksum_for_bytes (checksum_types[i], blob_fw);
		fu_device_add_checksum (dev, hash);
	}
	fu_plugin_set_status (plugin, FWUPD_STATUS_IDLE);
	return TRUE;
}

gboolean
fu_plugin_update (FuPlugin *plugin,
		  FuDevice *dev,
		  GBytes *blob_fw,
		  FwupdInstallFlags flags,
		  GError **error)
{
	fu_plugin_set_status (plugin, FWUPD_STATUS_DEVICE_WRITE);
	if (!fu_device_altos_write_firmware (FU_DEVICE_ALTOS (dev),
					     blob_fw,
					     FU_DEVICE_ALTOS_WRITE_FIRMWARE_FLAG_REBOOT,
					     fu_plugin_altos_progress_cb,
					     plugin,
					     error)) {
		return FALSE;
	}
	fu_plugin_set_status (plugin, FWUPD_STATUS_IDLE);
	return TRUE;
}

static void
fu_plugin_altos_device_added_cb (GUsbContext *ctx,
				    GUsbDevice *usb_device,
				    FuPlugin *plugin)
{
	g_autoptr(GError) error = NULL;
	if (!fu_plugin_altos_device_added (plugin, usb_device, &error)) {
		if (!g_error_matches (error,
				      FWUPD_ERROR,
				      FWUPD_ERROR_NOT_SUPPORTED)) {
			g_warning ("Failed to add altos device: %s",
				   error->message);
		}
	}
}

static void
fu_plugin_altos_device_removed_cb (GUsbContext *ctx,
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
			  G_CALLBACK (fu_plugin_altos_device_added_cb),
			  plugin);
	g_signal_connect (usb_ctx, "device-removed",
			  G_CALLBACK (fu_plugin_altos_device_removed_cb),
			  plugin);
	return TRUE;
}
