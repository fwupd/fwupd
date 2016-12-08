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
#include <glib-object.h>
#include <gusb.h>

#include "fu-ebitdo-device.h"
#include "fu-device.h"
#include "fu-provider-ebitdo.h"

static void	fu_provider_ebitdo_finalize	(GObject	*object);

typedef struct {
	GHashTable		*devices;		/* id : FuDevice */
	GHashTable		*devices_runtime;	/* id : FuEbitdoDevice */
	GUsbContext		*usb_ctx;
	gboolean		 done_enumerate;
} FuProviderEbitdoPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (FuProviderEbitdo, fu_provider_ebitdo, FU_TYPE_PROVIDER)
#define GET_PRIVATE(o) (fu_provider_ebitdo_get_instance_private (o))

static const gchar *
fu_provider_ebitdo_get_name (FuProvider *provider)
{
	return "ebitdo";
}

static gboolean
fu_provider_ebitdo_device_added (FuProviderEbitdo *provider_ebitdo,
				 GUsbDevice *usb_device,
				 GError **error)
{
	FuProviderEbitdoPrivate *priv = GET_PRIVATE (provider_ebitdo);
	FuEbitdoDeviceKind ebitdo_kind;
	const gchar *platform_id = NULL;
	g_autoptr(AsProfile) profile = as_profile_new ();
	g_autoptr(AsProfileTask) ptask = NULL;
	g_autoptr(FuEbitdoDevice) ebitdo_dev = NULL;
	g_autoptr(FuDevice) dev = NULL;
	g_autofree gchar *name = NULL;

	/* ignore hubs */
	ptask = as_profile_start (profile, "FuProviderEbitdo:added{%04x:%04x}",
				  g_usb_device_get_vid (usb_device),
				  g_usb_device_get_pid (usb_device));

	/* get version */
	platform_id = g_usb_device_get_platform_id (usb_device);
	ebitdo_dev = fu_ebitdo_device_new (usb_device);
	if (fu_ebitdo_device_get_kind (ebitdo_dev) == FU_EBITDO_DEVICE_KIND_UNKNOWN) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "invalid 8Bitdo device type detected");
		return FALSE;
	}

	/* open the device */
	if (!fu_ebitdo_device_open (ebitdo_dev, error))
		return FALSE;

	/* generate name */
	ebitdo_kind = fu_ebitdo_device_get_kind (ebitdo_dev);
	name = g_strdup_printf ("8Bitdo %s Gamepad",
				fu_ebitdo_device_kind_to_string (ebitdo_kind));

	/* create the device */
	dev = fu_device_new ();
	fu_device_set_id (dev, platform_id);
	fu_device_add_flag (dev, FWUPD_DEVICE_FLAG_ALLOW_ONLINE);
	fu_device_add_guid (dev, fu_ebitdo_device_get_guid (ebitdo_dev));
	fu_device_set_version (dev, fu_ebitdo_device_get_version (ebitdo_dev));
	fu_device_set_name (dev, name);

	/* close the device */
	if (!fu_ebitdo_device_close (ebitdo_dev, error))
		return FALSE;

	/* only the bootloader can do the update */
	if (ebitdo_kind == FU_EBITDO_DEVICE_KIND_BOOTLOADER) {
		FuEbitdoDevice *ebitdo_runtime;
		fu_device_remove_flag (dev, FWUPD_DEVICE_FLAG_NEEDS_BOOTLOADER);

		/* add the last seen runtime GUID too */
		ebitdo_runtime = g_hash_table_lookup (priv->devices_runtime, platform_id);
		if (ebitdo_runtime != NULL) {
			const gchar *guid;
			guid = fu_ebitdo_device_get_guid (ebitdo_runtime);
			g_debug ("adding runtime GUID of %s", guid);
			fu_device_add_guid (dev, guid);
		}
	} else {
		fu_device_add_flag (dev, FWUPD_DEVICE_FLAG_NEEDS_BOOTLOADER);
		g_hash_table_insert (priv->devices_runtime,
				     g_strdup (platform_id),
				     g_object_ref (ebitdo_dev));
		g_debug ("saving runtime GUID of %s",
			 fu_ebitdo_device_get_guid (ebitdo_dev));
	}

	/* insert to hash */
	fu_provider_device_add (FU_PROVIDER (provider_ebitdo), dev);
	g_hash_table_insert (priv->devices, g_strdup (platform_id), g_object_ref (dev));
	return TRUE;
}

static void
ebitdo_write_progress_cb (goffset current, goffset total, gpointer user_data)
{
	FuProvider *provider = FU_PROVIDER (user_data);
	gdouble percentage = -1.f;
	if (total > 0)
		percentage = (100.f * (gdouble) current) / (gdouble) total;
	g_debug ("written %" G_GOFFSET_FORMAT "/%" G_GOFFSET_FORMAT " bytes [%.1f%%]",
		 current, total, percentage);
	fu_provider_set_percentage (provider, (guint) percentage);
}

static gboolean
fu_provider_ebitdo_update (FuProvider *provider,
			   FuDevice *dev,
			   GBytes *blob_fw,
			   FwupdInstallFlags flags,
			   GError **error)
{
	FuProviderEbitdo *provider_ebitdo = FU_PROVIDER_EBITDO (provider);
	FuProviderEbitdoPrivate *priv = GET_PRIVATE (provider_ebitdo);
	const gchar *platform_id;
	g_autoptr(FuEbitdoDevice) ebitdo_dev = NULL;
	g_autoptr(GUsbDevice) usb_device = NULL;

	/* get version */
	platform_id = fu_device_get_id (dev);
	usb_device = g_usb_context_find_by_platform_id (priv->usb_ctx,
							platform_id,
							error);
	if (usb_device == NULL)
		return FALSE;
	ebitdo_dev = fu_ebitdo_device_new (usb_device);
	if (fu_ebitdo_device_get_kind (ebitdo_dev) != FU_EBITDO_DEVICE_KIND_BOOTLOADER) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "invalid 8Bitdo device type detected");
		return FALSE;
	}

	/* write the firmware */
	if (!fu_ebitdo_device_open (ebitdo_dev, error))
		return FALSE;
	fu_provider_set_status (provider, FWUPD_STATUS_DEVICE_WRITE);
	if (!fu_ebitdo_device_write_firmware (ebitdo_dev, blob_fw,
					   ebitdo_write_progress_cb, provider,
					   error))
		return FALSE;
	fu_provider_set_status (provider, FWUPD_STATUS_DEVICE_RESTART);
	if (!fu_ebitdo_device_close (ebitdo_dev, error))
		return FALSE;

	/* success */
	return TRUE;
}

static void
fu_provider_ebitdo_device_added_cb (GUsbContext *ctx,
				    GUsbDevice *usb_device,
				    FuProviderEbitdo *provider_ebitdo)
{
	g_autoptr(GError) error = NULL;
	if (!fu_provider_ebitdo_device_added (provider_ebitdo, usb_device, &error)) {
		if (!g_error_matches (error,
				      FWUPD_ERROR,
				      FWUPD_ERROR_NOT_SUPPORTED)) {
			g_warning ("Failed to add 8Bitdo device: %s",
				   error->message);
		}
	}
}

static void
fu_provider_ebitdo_device_removed_cb (GUsbContext *ctx,
				      GUsbDevice *usb_device,
				      FuProviderEbitdo *provider_ebitdo)
{
	FuProviderEbitdoPrivate *priv = GET_PRIVATE (provider_ebitdo);
	FuDevice *dev;
	const gchar *platform_id = NULL;

	/* already in database */
	platform_id = g_usb_device_get_platform_id (usb_device);
	dev = g_hash_table_lookup (priv->devices, platform_id);
	if (dev == NULL)
		return;

	fu_provider_device_remove (FU_PROVIDER (provider_ebitdo), dev);
	g_hash_table_remove (priv->devices, platform_id);
}

static gboolean
fu_provider_ebitdo_setup (FuProvider *provider, GError **error)
{
	FuProviderEbitdo *provider_ebitdo = FU_PROVIDER_EBITDO (provider);
	FuProviderEbitdoPrivate *priv = GET_PRIVATE (provider_ebitdo);
	priv->usb_ctx = fu_provider_get_usb_context (provider);
	g_signal_connect (priv->usb_ctx, "device-added",
			  G_CALLBACK (fu_provider_ebitdo_device_added_cb),
			  provider_ebitdo);
	g_signal_connect (priv->usb_ctx, "device-removed",
			  G_CALLBACK (fu_provider_ebitdo_device_removed_cb),
			  provider_ebitdo);
	return TRUE;
}

static gboolean
fu_provider_ebitdo_coldplug (FuProvider *provider, GError **error)
{
	FuProviderEbitdo *provider_ebitdo = FU_PROVIDER_EBITDO (provider);
	FuProviderEbitdoPrivate *priv = GET_PRIVATE (provider_ebitdo);
	priv->done_enumerate = TRUE;
	return TRUE;
}

static void
fu_provider_ebitdo_class_init (FuProviderEbitdoClass *klass)
{
	FuProviderClass *provider_class = FU_PROVIDER_CLASS (klass);
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	provider_class->get_name = fu_provider_ebitdo_get_name;
	provider_class->setup = fu_provider_ebitdo_setup;
	provider_class->coldplug = fu_provider_ebitdo_coldplug;
	provider_class->update_online = fu_provider_ebitdo_update;
	object_class->finalize = fu_provider_ebitdo_finalize;
}

static void
fu_provider_ebitdo_init (FuProviderEbitdo *provider_ebitdo)
{
	FuProviderEbitdoPrivate *priv = GET_PRIVATE (provider_ebitdo);
	priv->devices = g_hash_table_new_full (g_str_hash, g_str_equal,
					       g_free, (GDestroyNotify) g_object_unref);
	priv->devices_runtime = g_hash_table_new_full (g_str_hash, g_str_equal,
						       g_free, (GDestroyNotify) g_object_unref);
}

static void
fu_provider_ebitdo_finalize (GObject *object)
{
	FuProviderEbitdo *provider_ebitdo = FU_PROVIDER_EBITDO (object);
	FuProviderEbitdoPrivate *priv = GET_PRIVATE (provider_ebitdo);

	g_hash_table_unref (priv->devices);
	g_hash_table_unref (priv->devices_runtime);
	g_object_unref (priv->usb_ctx);

	G_OBJECT_CLASS (fu_provider_ebitdo_parent_class)->finalize (object);
}

FuProvider *
fu_provider_ebitdo_new (void)
{
	FuProviderEbitdo *provider;
	provider = g_object_new (FU_TYPE_PROVIDER_EBITDO, NULL);
	return FU_PROVIDER (provider);
}
