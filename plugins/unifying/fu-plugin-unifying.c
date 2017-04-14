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
#include <fwupd.h>
#include <gusb.h>

#include "fu-plugin.h"
#include "fu-plugin-vfuncs.h"

#include "lu-context.h"
#include "lu-device.h"

struct FuPluginData {
	LuContext		*ctx;
	gboolean		 ignore_replug;
};

static gboolean
fu_plugin_unifying_device_added (FuPlugin *plugin,
				 LuDevice *device,
				 GError **error)
{
	GPtrArray *guids;
	g_autofree gchar *name = NULL;
	g_autoptr(AsProfile) profile = as_profile_new ();
	g_autoptr(AsProfileTask) ptask = NULL;
	g_autoptr(FuDevice) dev = NULL;

	/* profile */
	ptask = as_profile_start (profile, "FuPluginLu:added{%s}",
				  lu_device_get_platform_id (device));
	g_assert (ptask != NULL);

	/* open the device */
	if (!lu_device_open (device, error))
		return FALSE;

	/* create new FuDevice */
	dev = fu_device_new ();
	if (lu_device_has_flag (device, LU_DEVICE_FLAG_CAN_FLASH))
		fu_device_add_flag (dev, FWUPD_DEVICE_FLAG_ALLOW_ONLINE);
	fu_device_set_id (dev, lu_device_get_platform_id (device));
	fu_device_set_name (dev, lu_device_get_product (device));
	fu_device_set_vendor (dev, lu_device_get_vendor (device));
	fu_device_set_version (dev, lu_device_get_version_fw (device));
	fu_device_set_version_bootloader (dev, lu_device_get_version_bl (device));
	guids = lu_device_get_guids (device);
	for (guint i = 0; i < guids->len; i++) {
		const gchar *guid = g_ptr_array_index (guids, i);
		fu_device_add_guid (dev, guid);
	}

	/* close the device */
	if (!lu_device_close (device, error))
		return FALSE;

	/* insert to hash */
	fu_plugin_device_add (plugin, dev);
	fu_plugin_cache_add (plugin, fu_device_get_id (dev), dev);
	return TRUE;
}

static void
lu_write_progress_cb (goffset current, goffset total, gpointer user_data)
{
	FuPlugin *plugin = FU_PLUGIN (user_data);
	gdouble percentage = -1.f;
	if (total > 0)
		percentage = (100.f * (gdouble) current) / (gdouble) total;
	g_debug ("written %" G_GOFFSET_FORMAT "/%" G_GOFFSET_FORMAT " bytes [%.1f%%]",
		 current, total, percentage);
	fu_plugin_set_percentage (plugin, (guint) percentage);
}

static LuDevice *
fu_plugin_unifying_get_device (FuPlugin *plugin,
			       FuDevice *dev,
			       GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	LuDevice *device;
	device = lu_context_find_by_platform_id (data->ctx, fu_device_get_id (dev), error);
	if (device == NULL)
		return NULL;
	return g_object_ref (device);
}

static gboolean
fu_plugin_unifying_detach_cb (gpointer user_data)
{
	LuDevice *device = LU_DEVICE (user_data);
	g_autoptr(GError) error = NULL;

	/* ditch this device */
	g_debug ("detaching");
	if (!lu_device_detach (device, &error)) {
		g_warning ("failed to detach: %s", error->message);
		return FALSE;
	}
	if (!lu_device_close (device, &error)) {
		g_warning ("failed to close: %s", error->message);
		return FALSE;
	}

	return FALSE;
}

gboolean
fu_plugin_update_online (FuPlugin *plugin,
			 FuDevice *dev,
			 GBytes *blob_fw,
			 FwupdInstallFlags flags,
			 GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	g_autoptr(LuDevice) device = NULL;

	/* get version */
	device = fu_plugin_unifying_get_device (plugin, dev, error);
	if (device == NULL)
		return FALSE;
	if (!lu_device_open (device, error))
		return FALSE;

	/* switch to bootloader */
	data->ignore_replug = TRUE;
	if (lu_device_get_kind (device) == LU_DEVICE_KIND_RUNTIME) {
		/* wait for device to come back */
		g_timeout_add (50, fu_plugin_unifying_detach_cb, device);
		if (!lu_context_wait_for_replug (data->ctx,
						 device,
						 2000,
						 error))
			return FALSE;
		g_object_unref (device);
		device = fu_plugin_unifying_get_device (plugin, dev, error);
		if (device == NULL)
			return FALSE;
		if (!lu_device_open (device, error))
			return FALSE;
	}

	/* write the firmware */
	fu_plugin_set_status (plugin, FWUPD_STATUS_DEVICE_WRITE);
	if (!lu_device_write_firmware (device, blob_fw,
					     lu_write_progress_cb, plugin,
					     error))
		return FALSE;
	fu_plugin_set_status (plugin, FWUPD_STATUS_DEVICE_RESTART);
	if (!lu_device_attach (device, error))
		return FALSE;
	if (!lu_device_close (device, error))
		return FALSE;

	/* success */
	data->ignore_replug = FALSE;
	return TRUE;
}

static void
fu_plugin_unifying_device_added_cb (LuContext *ctx,
				    LuDevice *device,
				    FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	g_autoptr(GError) error = NULL;

	/* in process of flashing */
	if (data->ignore_replug)
		return;

	/* add */
	if (!fu_plugin_unifying_device_added (plugin, device, &error)) {
		if (!g_error_matches (error,
				      FWUPD_ERROR,
				      FWUPD_ERROR_NOT_SUPPORTED)) {
			g_warning ("Failed to add Logitech device: %s",
				   error->message);
		}
	}
}

static void
fu_plugin_unifying_device_removed_cb (LuContext *ctx,
				      LuDevice *device,
				      FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	FuDevice *dev;
	const gchar *platform_id = NULL;

	/* in process of flashing */
	if (data->ignore_replug)
		return;

	/* already in database */
	platform_id = lu_device_get_platform_id (device);
	dev = fu_plugin_cache_lookup (plugin, platform_id);
	if (dev == NULL)
		return;

	fu_plugin_device_remove (plugin, dev);
	fu_plugin_cache_remove (plugin, platform_id);
}

gboolean
fu_plugin_startup (FuPlugin *plugin, GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	g_signal_connect (data->ctx, "added",
			  G_CALLBACK (fu_plugin_unifying_device_added_cb),
			  plugin);
	g_signal_connect (data->ctx, "removed",
			  G_CALLBACK (fu_plugin_unifying_device_removed_cb),
			  plugin);
	lu_context_coldplug (data->ctx);
	return TRUE;
}

void
fu_plugin_init (FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_alloc_data (plugin, sizeof (FuPluginData));
	GUsbContext *usb_ctx = fu_plugin_get_usb_context (plugin);
	data->ctx = lu_context_new_full (usb_ctx);
}

void
fu_plugin_destroy (FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	g_object_unref (data->ctx);
}
