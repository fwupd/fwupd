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

#include <fwupd.h>
#include <glib-object.h>
#include <gusb.h>

#include "fu-cleanup.h"
#include "fu-device.h"
#include "fu-provider-usb.h"

static void     fu_provider_usb_finalize	(GObject	*object);

#define FU_PROVIDER_USB_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), FU_TYPE_PROVIDER_USB, FuProviderUsbPrivate))

/**
 * FuProviderUsbPrivate:
 **/
struct _FuProviderUsbPrivate
{
	GHashTable		*devices;
	GUsbContext		*usb_ctx;
};

G_DEFINE_TYPE (FuProviderUsb, fu_provider_usb, FU_TYPE_PROVIDER)

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
 * fu_provider_usb_device_added_cb:
 **/
static void
fu_provider_usb_device_added_cb (GUsbContext *ctx,
				 GUsbDevice *device,
				 FuProviderUsb *provider_usb)
{
	FuDevice *dev;
	guint8 idx = 0x00;
	_cleanup_error_free_ GError *error = NULL;
	_cleanup_free_ gchar *guid = NULL;
	_cleanup_free_ gchar *id = NULL;
	_cleanup_free_ gchar *product = NULL;
	_cleanup_free_ gchar *version = NULL;

	/* ignore hubs */
#if G_USB_CHECK_VERSION(0,2,5)
	if (g_usb_device_get_device_class (device) == G_USB_DEVICE_CLASS_HUB)
		return;
#endif

	/* handled by another provider */
	id = fu_provider_usb_get_id (device);
	if (g_usb_device_get_vid (device) == 0x273f) {
		g_debug ("handling %s in another provider", id);
		return;
	}

	/* is already in database */
	dev = g_hash_table_lookup (provider_usb->priv->devices, id);
	if (dev != NULL) {
		g_debug ("ignoring duplicate %s", id);
		return;
	}

	/* try to get the version without claiming interface */
	if (!g_usb_device_open (device, &error)) {
		g_debug ("Failed to open: %s", error->message);
		return;
	}
#if G_USB_CHECK_VERSION(0,2,5)
	idx = g_usb_device_get_custom_index (device,
					     G_USB_DEVICE_CLASS_VENDOR_SPECIFIC,
					     'F', 'W', NULL);
#endif
	if (idx != 0x00)
		version = g_usb_device_get_string_descriptor (device, idx, NULL);
#if G_USB_CHECK_VERSION(0,2,5)
	idx = g_usb_device_get_custom_index (device,
					     G_USB_DEVICE_CLASS_VENDOR_SPECIFIC,
					     'G', 'U', NULL);
#endif
	if (idx != 0x00)
		guid = g_usb_device_get_string_descriptor (device, idx, NULL);
	idx = g_usb_device_get_product_index (device);
	if (idx != 0x00)
		product = g_usb_device_get_string_descriptor (device, idx, NULL);

	/* did we get enough data */
	if (version != NULL && guid != NULL && product != NULL) {
		dev = fu_device_new ();
		fu_device_set_id (dev, id);
		fu_device_set_guid (dev, guid);
		fu_device_set_display_name (dev, product);
		fu_device_set_metadata (dev, FU_DEVICE_KEY_VERSION, version);

		/* insert to hash */
		g_hash_table_insert (provider_usb->priv->devices,
				     g_strdup (id), dev);
		fu_provider_device_add (FU_PROVIDER (provider_usb), dev);
	} else {
		g_debug ("ignoring %s [%s:%s:%s]", id,
			 product != NULL ? product : "",
			 version != NULL ? version : "",
			 guid != NULL ? guid : "");
	}

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
	FuDevice *dev;
	_cleanup_free_ gchar *id = NULL;

	/* already in database */
	id = fu_provider_usb_get_id (device);
	dev = g_hash_table_lookup (provider_usb->priv->devices, id);
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
	g_usb_context_enumerate (provider_usb->priv->usb_ctx);
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

	g_type_class_add_private (klass, sizeof (FuProviderUsbPrivate));
}

/**
 * fu_provider_usb_init:
 **/
static void
fu_provider_usb_init (FuProviderUsb *provider_usb)
{
	provider_usb->priv = FU_PROVIDER_USB_GET_PRIVATE (provider_usb);
	provider_usb->priv->devices = g_hash_table_new_full (g_str_hash, g_str_equal,
							      g_free, (GDestroyNotify) g_object_unref);
	provider_usb->priv->usb_ctx = g_usb_context_new (NULL);
	g_signal_connect (provider_usb->priv->usb_ctx, "device-added",
			  G_CALLBACK (fu_provider_usb_device_added_cb),
			  provider_usb);
	g_signal_connect (provider_usb->priv->usb_ctx, "device-removed",
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
	FuProviderUsbPrivate *priv = provider_usb->priv;

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
