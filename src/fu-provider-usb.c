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

/**
 * FuProviderUsbPrivate:
 **/
typedef struct {
	GHashTable		*devices;
	GUsbContext		*usb_ctx;
} FuProviderUsbPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (FuProviderUsb, fu_provider_usb, FU_TYPE_PROVIDER)
#define GET_PRIVATE(o) (fu_provider_usb_get_instance_private (o))

/**
 * fu_provider_usb_get_name:
 **/
static const gchar *
fu_provider_usb_get_name (FuProvider *provider)
{
	return "USB";
}

/**
 * fu_provider_usb_get_id:
 **/
static gchar *
fu_provider_usb_get_id (GUsbDevice *device)
{
	/* this identifies the *port* the device is plugged into */
	return g_strdup_printf ("ro-%s", g_usb_device_get_platform_id (device));
}

/**
 * fu_provider_usb_device_add:
 *
 * Important, the device must already be open!
 **/
static void
fu_provider_usb_device_add (FuProviderUsb *provider_usb, const gchar *id, GUsbDevice *device)
{
	FuProviderUsbPrivate *priv = GET_PRIVATE (provider_usb);
	FuDevice *dev;
	guint8 idx = 0x00;
	g_autofree gchar *guid = NULL;
	g_autofree gchar *product = NULL;
	g_autofree gchar *version = NULL;
	g_autoptr(AsProfile) profile = as_profile_new ();
	g_autoptr(AsProfileTask) ptask = NULL;

	/* get product */
	idx = g_usb_device_get_product_index (device);
	if (idx != 0x00) {
		g_autoptr(AsProfileTask) ptask2 = NULL;
		ptask2 = as_profile_start_literal (profile, "FuProviderUsb:get-string-desc");
		product = g_usb_device_get_string_descriptor (device, idx, NULL);
	}
	if (product == NULL) {
		g_debug ("ignoring %s as no product string descriptor", id);
		return;
	}

	ptask = as_profile_start_literal (profile, "FuProviderUsb:get-custom-index");
#if G_USB_CHECK_VERSION(0,2,5)
	idx = g_usb_device_get_custom_index (device,
					     G_USB_DEVICE_CLASS_VENDOR_SPECIFIC,
					     'F', 'W', NULL);
#endif
	if (idx != 0x00)
		version = g_usb_device_get_string_descriptor (device, idx, NULL);
	if (version == NULL) {
		g_debug ("ignoring %s [%s] as no version", id, product);
		return;
	}
#if G_USB_CHECK_VERSION(0,2,5)
	idx = g_usb_device_get_custom_index (device,
					     G_USB_DEVICE_CLASS_VENDOR_SPECIFIC,
					     'G', 'U', NULL);
#endif
	if (idx != 0x00)
		guid = g_usb_device_get_string_descriptor (device, idx, NULL);
	if (guid == NULL) {
		g_debug ("ignoring %s [%s] as no GUID", id, product);
		return;
	}

	/* insert to hash */
	dev = fu_device_new ();
	fu_device_set_id (dev, id);
	fu_device_set_guid (dev, guid);
	fu_device_set_display_name (dev, product);
	fu_device_set_metadata (dev, FU_DEVICE_KEY_VERSION, version);
	g_hash_table_insert (priv->devices,
			     g_strdup (id), dev);
	fu_provider_device_add (FU_PROVIDER (provider_usb), dev);
}

/**
 * fu_provider_usb_device_added_cb:
 **/
static void
fu_provider_usb_device_added_cb (GUsbContext *ctx,
				 GUsbDevice *device,
				 FuProviderUsb *provider_usb)
{
	FuProviderUsbPrivate *priv = GET_PRIVATE (provider_usb);
	FuDevice *dev;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *id = NULL;
	g_autoptr(AsProfile) profile = as_profile_new ();
	g_autoptr(AsProfileTask) ptask = NULL;

	/* ignore hubs */
#if G_USB_CHECK_VERSION(0,2,5)
	if (g_usb_device_get_device_class (device) == G_USB_DEVICE_CLASS_HUB)
		return;
#endif
	ptask = as_profile_start (profile, "FuProviderUsb:added{%04x:%04x}",
				  g_usb_device_get_vid (device),
				  g_usb_device_get_pid (device));

	/* handled by another provider */
	id = fu_provider_usb_get_id (device);
	if (g_usb_device_get_vid (device) == 0x273f) {
		g_debug ("handling %s in another provider", id);
		return;
	}

	/* is already in database */
	dev = g_hash_table_lookup (priv->devices, id);
	if (dev != NULL) {
		g_debug ("ignoring duplicate %s", id);
		return;
	}

	/* try to get the version without claiming interface */
	if (!g_usb_device_open (device, &error)) {
		g_debug ("Failed to open: %s", error->message);
		return;
	}

	/* try to add the device */
	fu_provider_usb_device_add (provider_usb, id, device);

	/* we're done here */
	if (!g_usb_device_close (device, &error))
		g_debug ("Failed to close: %s", error->message);
}

/**
 * fu_provider_usb_device_removed_cb:
 **/
static void
fu_provider_usb_device_removed_cb (GUsbContext *ctx,
				   GUsbDevice *device,
				   FuProviderUsb *provider_usb)
{
	FuProviderUsbPrivate *priv = GET_PRIVATE (provider_usb);
	FuDevice *dev;
	g_autofree gchar *id = NULL;

	/* already in database */
	id = fu_provider_usb_get_id (device);
	dev = g_hash_table_lookup (priv->devices, id);
	if (dev == NULL)
		return;
	fu_provider_device_remove (FU_PROVIDER (provider_usb), dev);
}

/**
 * fu_provider_usb_coldplug:
 **/
static gboolean
fu_provider_usb_coldplug (FuProvider *provider, GError **error)
{
	FuProviderUsb *provider_usb = FU_PROVIDER_USB (provider);
	FuProviderUsbPrivate *priv = GET_PRIVATE (provider_usb);

	g_usb_context_enumerate (priv->usb_ctx);
	return TRUE;
}

/**
 * fu_provider_usb_class_init:
 **/
static void
fu_provider_usb_class_init (FuProviderUsbClass *klass)
{
	FuProviderClass *provider_class = FU_PROVIDER_CLASS (klass);
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	provider_class->get_name = fu_provider_usb_get_name;
	provider_class->coldplug = fu_provider_usb_coldplug;
	object_class->finalize = fu_provider_usb_finalize;
}

/**
 * fu_provider_usb_init:
 **/
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

/**
 * fu_provider_usb_finalize:
 **/
static void
fu_provider_usb_finalize (GObject *object)
{
	FuProviderUsb *provider_usb = FU_PROVIDER_USB (object);
	FuProviderUsbPrivate *priv = GET_PRIVATE (provider_usb);

	g_hash_table_unref (priv->devices);
	g_object_unref (priv->usb_ctx);

	G_OBJECT_CLASS (fu_provider_usb_parent_class)->finalize (object);
}

/**
 * fu_provider_usb_new:
 **/
FuProvider *
fu_provider_usb_new (void)
{
	FuProviderUsb *provider;
	provider = g_object_new (FU_TYPE_PROVIDER_USB, NULL);
	return FU_PROVIDER (provider);
}
