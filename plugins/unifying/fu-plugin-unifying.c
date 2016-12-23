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

#include "unifying-dongle.h"

static gboolean
fu_plugin_unifying_device_added (FuPlugin *plugin,
				 GUsbDevice *usb_device,
				 GError **error)
{
	UnifyingDongleKind fu_unifying_kind;
	const gchar *platform_id = NULL;
	g_autofree gchar *name = NULL;
	g_autoptr(AsProfile) profile = as_profile_new ();
	g_autoptr(AsProfileTask) ptask = NULL;
	g_autoptr(FuDevice) dev = NULL;
	g_autoptr(UnifyingDongle) dongle = NULL;

	/* profile */
	ptask = as_profile_start (profile, "FuPluginUnifying:added{%04x:%04x}",
				  g_usb_device_get_vid (usb_device),
				  g_usb_device_get_pid (usb_device));
	g_assert (ptask != NULL);

	/* get version */
	platform_id = g_usb_device_get_platform_id (usb_device);
	dongle = unifying_dongle_new (usb_device);
	if (dongle == NULL ||
	    unifying_dongle_get_kind (dongle) == UNIFYING_DONGLE_KIND_UNKNOWN) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "invalid Logitech device type detected");
		return FALSE;
	}

	/* open the device */
	if (!unifying_dongle_open (dongle, error))
		return FALSE;

	/* generate name */
	fu_unifying_kind = unifying_dongle_get_kind (dongle);
	name = g_strdup_printf ("Logitech Unifying [%s]",
				unifying_dongle_kind_to_string (fu_unifying_kind));

	/* create the device */
	dev = fu_device_new ();
	fu_device_set_id (dev, platform_id);
	fu_device_add_flag (dev, FWUPD_DEVICE_FLAG_ALLOW_ONLINE);
	fu_device_add_guid (dev, unifying_dongle_get_guid (dongle));
	fu_device_set_version (dev, unifying_dongle_get_version_fw (dongle));
	fu_device_set_version_bootloader (dev, unifying_dongle_get_version_bl (dongle));
	fu_device_set_name (dev, name);

	/* close the device */
	if (!unifying_dongle_close (dongle, error))
		return FALSE;

	/* insert to hash */
	fu_plugin_device_add (plugin, dev);
	fu_plugin_cache_add (plugin, platform_id, dev);
	return TRUE;
}

static void
fu_unifying_write_progress_cb (goffset current, goffset total, gpointer user_data)
{
	FuPlugin *plugin = FU_PLUGIN (user_data);
	gdouble percentage = -1.f;
	if (total > 0)
		percentage = (100.f * (gdouble) current) / (gdouble) total;
	g_debug ("written %" G_GOFFSET_FORMAT "/%" G_GOFFSET_FORMAT " bytes [%.1f%%]",
		 current, total, percentage);
	fu_plugin_set_percentage (plugin, (guint) percentage);
}

static UnifyingDongle *
fu_plugin_unifying_get_dongle (FuPlugin *plugin,
			       FuDevice *dev,
			       GError **error)
{
	GUsbContext *usb_ctx = fu_plugin_get_usb_context (plugin);
	const gchar *platform_id;
	g_autoptr(GUsbDevice) usb_device = NULL;
	g_autoptr(UnifyingDongle) dongle = NULL;

	/* get version */
	platform_id = fu_device_get_id (dev);
	usb_device = g_usb_context_find_by_platform_id (usb_ctx,
							platform_id,
							error);
	if (usb_device == NULL)
		return NULL;
	dongle = unifying_dongle_new (usb_device);
	if (dongle == NULL)
		return NULL;
	if (unifying_dongle_get_kind (dongle) == UNIFYING_DONGLE_KIND_UNKNOWN) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "invalid Logitech device type detected");
		return NULL;
	}
	return g_steal_pointer (&dongle);
}

static gboolean
fu_plugin_unifying_detach_cb (gpointer user_data)
{
	UnifyingDongle *dongle = UNIFYING_DONGLE (user_data);
	g_autoptr(GError) error = NULL;

	/* ditch this device */
	g_debug ("detaching");
	if (!unifying_dongle_detach (dongle, &error)) {
		g_warning ("failed to detach: %s", error->message);
		return FALSE;
	}
	if (!unifying_dongle_close (dongle, &error)) {
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
	GUsbContext *usb_ctx = fu_plugin_get_usb_context (plugin);
	g_autoptr(UnifyingDongle) dongle = NULL;

	/* get version */
	dongle = fu_plugin_unifying_get_dongle (plugin, dev, error);
	if (dongle == NULL)
		return FALSE;
	if (!unifying_dongle_open (dongle, error))
		return FALSE;

	/* switch to bootloader */
	if (unifying_dongle_get_kind (dongle) == UNIFYING_DONGLE_KIND_RUNTIME) {
		g_autoptr(GUsbDevice) usb_device = NULL;

		/* wait for device to come back */
		g_timeout_add (50, fu_plugin_unifying_detach_cb, dongle);
		usb_device = g_usb_context_wait_for_replug (usb_ctx,
							    unifying_dongle_get_usb_device (dongle),
							    2000,
							    error);
		if (usb_device == NULL)
			return FALSE;

		/* find new device */
		g_object_unref (dongle);
		dongle = unifying_dongle_new (usb_device);
		if (dongle == NULL)
			return FALSE;
		if (!unifying_dongle_open (dongle, error))
			return FALSE;
	}

	/* write the firmware */
	fu_plugin_set_status (plugin, FWUPD_STATUS_DEVICE_WRITE);
	if (!unifying_dongle_write_firmware (dongle, blob_fw,
					     fu_unifying_write_progress_cb, plugin,
					     error))
		return FALSE;
	fu_plugin_set_status (plugin, FWUPD_STATUS_DEVICE_RESTART);
	if (!unifying_dongle_attach (dongle, error))
		return FALSE;
	if (!unifying_dongle_close (dongle, error))
		return FALSE;

	/* success */
	return TRUE;
}

static void
fu_plugin_unifying_device_added_cb (GUsbContext *ctx,
				    GUsbDevice *usb_device,
				    FuPlugin *plugin)
{
	g_autoptr(GError) error = NULL;
	if (!fu_plugin_unifying_device_added (plugin, usb_device, &error)) {
		if (!g_error_matches (error,
				      FWUPD_ERROR,
				      FWUPD_ERROR_NOT_SUPPORTED)) {
			g_warning ("Failed to add Logitech device: %s",
				   error->message);
		}
	}
}

static void
fu_plugin_unifying_device_removed_cb (GUsbContext *ctx,
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
			  G_CALLBACK (fu_plugin_unifying_device_added_cb),
			  plugin);
	g_signal_connect (usb_ctx, "device-removed",
			  G_CALLBACK (fu_plugin_unifying_device_removed_cb),
			  plugin);
	return TRUE;
}
