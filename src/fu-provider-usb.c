/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
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

#include "fu-device.h"
#include "fu-provider-usb.h"

static void	fu_provider_usb_finalize	(GObject	*object);

typedef struct {
	GHashTable		*devices;
	GUsbContext		*usb_ctx;
	gboolean		 done_enumerate;
} FuProviderUsbPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (FuProviderUsb, fu_provider_usb, FU_TYPE_PROVIDER)
#define GET_PRIVATE(o) (fu_provider_usb_get_instance_private (o))

static const gchar *
fu_provider_usb_get_name (FuProvider *provider)
{
	return "USB";
}

static void
fu_provider_usb_device_added (FuProviderUsb *provider_usb, GUsbDevice *device)
{
	FuProviderUsbPrivate *priv = GET_PRIVATE (provider_usb);
	const gchar *platform_id = NULL;
	guint8 idx = 0x00;
	g_autofree gchar *devid1 = NULL;
	g_autofree gchar *devid2 = NULL;
	g_autofree gchar *product = NULL;
	g_autofree gchar *version = NULL;
	g_autoptr(AsProfile) profile = as_profile_new ();
	g_autoptr(AsProfileTask) ptask = NULL;
	g_autoptr(FuDevice) dev = NULL;
	g_autoptr(GError) error = NULL;

	/* ignore hubs */
	if (g_usb_device_get_device_class (device) == G_USB_DEVICE_CLASS_HUB)
		return;
	ptask = as_profile_start (profile, "FuProviderUsb:added{%04x:%04x}",
				  g_usb_device_get_vid (device),
				  g_usb_device_get_pid (device));

	/* is already in database */
	platform_id = g_usb_device_get_platform_id (device);
	dev = g_hash_table_lookup (priv->devices, platform_id);
	if (dev != NULL) {
		g_debug ("ignoring duplicate %s", platform_id);
		return;
	}

	/* try to get the version without claiming interface */
	if (!g_usb_device_open (device, &error)) {
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
		ptask2 = as_profile_start_literal (profile, "FuProviderUsb:get-string-desc");
		product = g_usb_device_get_string_descriptor (device, idx, NULL);
	}
	if (product == NULL) {
		g_debug ("no product string descriptor");
		g_usb_device_close (device, NULL);
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
		guint16 release;
		release = g_usb_device_get_release (device);
		version = as_utils_version_from_uint16 (release,
							AS_VERSION_PARSE_FLAG_NONE);
	}
	fu_device_set_version (dev, version);

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

	/* we're done here */
	if (!g_usb_device_close (device, &error))
		g_debug ("Failed to close: %s", error->message);

	/* insert to hash */
	fu_provider_device_add (FU_PROVIDER (provider_usb), dev);
	g_hash_table_insert (priv->devices, g_strdup (platform_id), g_object_ref (dev));
}

typedef struct {
	FuProviderUsb	*provider_usb;
	GUsbDevice	*device;
} FuProviderUsbHelper;

static gboolean
fu_provider_usb_device_added_delay_cb (gpointer user_data)
{
	FuProviderUsbHelper *helper = (FuProviderUsbHelper *) user_data;
	fu_provider_usb_device_added (helper->provider_usb, helper->device);
	g_object_unref (helper->provider_usb);
	g_object_unref (helper->device);
	g_free (helper);
	return FALSE;
}

static void
fu_provider_usb_device_added_cb (GUsbContext *ctx,
				 GUsbDevice *device,
				 FuProviderUsb *provider_usb)
{
	FuProviderUsbPrivate *priv = GET_PRIVATE (provider_usb);

	/* use a small delay for hotplugging so that other, better, providers
	 * can claim this interface and add the FuDevice */
	if (priv->done_enumerate) {
		FuProviderUsbHelper *helper;
		g_debug ("waiting a small time for other providers");
		helper = g_new0 (FuProviderUsbHelper, 1);
		helper->provider_usb = g_object_ref (provider_usb);
		helper->device = g_object_ref (device);
		g_timeout_add (500, fu_provider_usb_device_added_delay_cb, helper);
		return;
	}
	fu_provider_usb_device_added (provider_usb, device);
}

static void
fu_provider_usb_device_removed_cb (GUsbContext *ctx,
				   GUsbDevice *device,
				   FuProviderUsb *provider_usb)
{
	FuProviderUsbPrivate *priv = GET_PRIVATE (provider_usb);
	FuDevice *dev;
	const gchar *platform_id = NULL;

	/* already in database */
	platform_id = g_usb_device_get_platform_id (device);
	dev = g_hash_table_lookup (priv->devices, platform_id);
	if (dev == NULL)
		return;

	fu_provider_device_remove (FU_PROVIDER (provider_usb), dev);
	g_hash_table_remove (priv->devices, platform_id);
}

static gboolean
fu_provider_usb_coldplug (FuProvider *provider, GError **error)
{
	FuProviderUsb *provider_usb = FU_PROVIDER_USB (provider);
	FuProviderUsbPrivate *priv = GET_PRIVATE (provider_usb);
	g_usb_context_enumerate (priv->usb_ctx);
	priv->done_enumerate = TRUE;
	return TRUE;
}

static void
fu_provider_usb_class_init (FuProviderUsbClass *klass)
{
	FuProviderClass *provider_class = FU_PROVIDER_CLASS (klass);
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	provider_class->get_name = fu_provider_usb_get_name;
	provider_class->coldplug = fu_provider_usb_coldplug;
	object_class->finalize = fu_provider_usb_finalize;
}

static void
fu_provider_usb_init (FuProviderUsb *provider_usb)
{
	FuProviderUsbPrivate *priv = GET_PRIVATE (provider_usb);
	priv->devices = g_hash_table_new_full (g_str_hash, g_str_equal,
					       g_free, (GDestroyNotify) g_object_unref);
	priv->usb_ctx = g_usb_context_new (NULL);
	g_signal_connect (priv->usb_ctx, "device-added",
			  G_CALLBACK (fu_provider_usb_device_added_cb),
			  provider_usb);
	g_signal_connect (priv->usb_ctx, "device-removed",
			  G_CALLBACK (fu_provider_usb_device_removed_cb),
			  provider_usb);
}

static void
fu_provider_usb_finalize (GObject *object)
{
	FuProviderUsb *provider_usb = FU_PROVIDER_USB (object);
	FuProviderUsbPrivate *priv = GET_PRIVATE (provider_usb);

	g_hash_table_unref (priv->devices);
	g_object_unref (priv->usb_ctx);

	G_OBJECT_CLASS (fu_provider_usb_parent_class)->finalize (object);
}

FuProvider *
fu_provider_usb_new (void)
{
	FuProviderUsb *provider;
	provider = g_object_new (FU_TYPE_PROVIDER_USB, NULL);
	return FU_PROVIDER (provider);
}
