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

#include <glib-object.h>
#include <gio/gio.h>

#include "fu-cleanup.h"
#include "fu-device.h"
#include "fu-provider-uefi.h"

static void     fu_provider_uefi_finalize	(GObject	*object);

#define FU_PROVIDER_UEFI_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), FU_TYPE_PROVIDER_UEFI, FuProviderUefiPrivate))

/**
 * FuProviderUefiPrivate:
 **/
struct _FuProviderUefiPrivate
{
	GPtrArray			*array_devices;
};

enum {
	SIGNAL_SENSOR_ADDED,
	SIGNAL_SENSOR_REMOVED,
	SIGNAL_LAST
};

static guint signals[SIGNAL_LAST] = { 0 };

G_DEFINE_TYPE (FuProviderUefi, fu_provider_uefi, G_TYPE_OBJECT)

/**
 * fu_provider_uefi_get_by_id:
 **/
static FuDevice *
fu_provider_uefi_get_by_id (FuProviderUefi *provider_uefi,
			    const gchar *device_id)
{
	FuProviderUefiPrivate *priv = provider_uefi->priv;
	FuDevice *device = NULL;
	FuDevice *device_tmp;
	guint i;

	for (i = 0; i < priv->array_devices->len; i++) {
		device_tmp = g_ptr_array_index (priv->array_devices, i);
		if (g_strcmp0 (fu_device_get_id (device_tmp), device_id) == 0) {
			device = g_object_ref (device_tmp);
			break;
		}
	}
	return device;
}

/**
 * fu_provider_uefi_coldplug:
 **/
void
fu_provider_uefi_coldplug (FuProviderUefi *provider_uefi)
{
	_cleanup_object_unref_ FuDevice *dev = NULL;

	//FIXME
	g_debug ("Adding fake UEFI device");
	dev = fu_device_new ();
	fu_device_set_id (dev, "819b858e-c52c-402f-80e1-5b311b6c1959");
	g_signal_emit (provider_uefi, signals[SIGNAL_SENSOR_ADDED], 0, dev);
}

/**
 * fu_provider_uefi_class_init:
 **/
static void
fu_provider_uefi_class_init (FuProviderUefiClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = fu_provider_uefi_finalize;
	signals[SIGNAL_SENSOR_ADDED] =
		g_signal_new ("device-added",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (FuProviderUefiClass, device_added),
			      NULL, NULL, g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE, 1, FU_TYPE_DEVICE);
	signals[SIGNAL_SENSOR_REMOVED] =
		g_signal_new ("device-removed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (FuProviderUefiClass, device_removed),
			      NULL, NULL, g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE, 1, FU_TYPE_DEVICE);

	g_type_class_add_private (klass, sizeof (FuProviderUefiPrivate));
}

/**
 * fu_provider_uefi_init:
 **/
static void
fu_provider_uefi_init (FuProviderUefi *provider_uefi)
{
	provider_uefi->priv = FU_PROVIDER_UEFI_GET_PRIVATE (provider_uefi);
	provider_uefi->priv->array_devices = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
}

/**
 * fu_provider_uefi_finalize:
 **/
static void
fu_provider_uefi_finalize (GObject *object)
{
	FuProviderUefi *provider_uefi = FU_PROVIDER_UEFI (object);
	FuProviderUefiPrivate *priv = provider_uefi->priv;

	g_ptr_array_unref (priv->array_devices);

	G_OBJECT_CLASS (fu_provider_uefi_parent_class)->finalize (object);
}

/**
 * fu_provider_uefi_new:
 **/
FuProviderUefi *
fu_provider_uefi_new (void)
{
	FuProviderUefi *provider_uefi;
	provider_uefi = g_object_new (FU_TYPE_PROVIDER_UEFI, NULL);
	return FU_PROVIDER_UEFI (provider_uefi);
}

