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

static void fu_device_finalize			 (GObject *object);

#define FU_DEVICE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), FU_TYPE_DEVICE, FuDevicePrivate))

/**
 * FuDevicePrivate:
 *
 * Private #FuDevice data
 **/
struct _FuDevicePrivate
{
	gchar				*id;
	GHashTable			*metadata;
};

enum {
	SIGNAL_LAST
};

enum {
	PROP_0,
	PROP_ID,
	PROP_LAST
};

G_DEFINE_TYPE (FuDevice, fu_device, G_TYPE_OBJECT)

/**
 * fu_device_get_id:
 **/
const gchar *
fu_device_get_id (FuDevice *device)
{
	g_return_val_if_fail (FU_IS_DEVICE (device), NULL);
	return device->priv->id;
}

/**
 * fu_device_set_id:
 **/
void
fu_device_set_id (FuDevice *device, const gchar *id)
{
	g_return_if_fail (FU_IS_DEVICE (device));

	g_free (device->priv->id);
	device->priv->id = g_strdup (id);
}

/**
 * fu_device_get_guid:
 **/
const gchar *
fu_device_get_guid (FuDevice *device)
{
	return fu_device_get_metadata (device, FU_DEVICE_KEY_GUID);
}

/**
 * fu_device_set_guid:
 **/
void
fu_device_set_guid (FuDevice *device, const gchar *guid)
{
	fu_device_set_metadata (device, FU_DEVICE_KEY_GUID, guid);
}

/**
 * fu_device_get_display_name:
 **/
const gchar *
fu_device_get_display_name (FuDevice *device)
{
	const gchar *tmp;
	tmp = fu_device_get_metadata (device, FU_DEVICE_KEY_DISPLAY_NAME);
	if (tmp == NULL)
		tmp = fu_device_get_id (device);
	return tmp;
}

/**
 * fu_device_set_display_name:
 **/
void
fu_device_set_display_name (FuDevice *device, const gchar *display_name)
{
	fu_device_set_metadata (device, FU_DEVICE_KEY_DISPLAY_NAME, display_name);
}

/**
 * fu_device_get_metadata:
 **/
const gchar *
fu_device_get_metadata (FuDevice *device, const gchar *key)
{
	g_return_val_if_fail (FU_IS_DEVICE (device), NULL);
	g_return_val_if_fail (key != NULL, NULL);
	return g_hash_table_lookup (device->priv->metadata, key);
}

/**
 * fu_device_get_id:
 **/
void
fu_device_set_metadata (FuDevice *device, const gchar *key, const gchar *value)
{
	g_return_if_fail (FU_IS_DEVICE (device));
	g_return_if_fail (key != NULL);
	g_return_if_fail (value != NULL);
	g_hash_table_insert (device->priv->metadata, g_strdup (key), g_strdup (value));
}

/**
 * fu_device_to_variant:
 **/
GVariant *
fu_device_to_variant (FuDevice *device)
{
	GList *l;
	GVariantBuilder builder;
	const gchar *key;
	const gchar *value;
	_cleanup_list_free_ GList *keys = NULL;

	/* create an array with all the metadata in */
	g_variant_builder_init (&builder, G_VARIANT_TYPE_ARRAY);
	keys = g_hash_table_get_keys (device->priv->metadata);
	for (l = keys; l != NULL; l = l->next) {
		key = l->data;
		value = g_hash_table_lookup (device->priv->metadata, key);
		if (g_strcmp0 (value, "TRUE") == 0) {
			g_variant_builder_add (&builder, "{sv}",
					       key, g_variant_new_boolean (TRUE));
		} else {
			g_variant_builder_add (&builder, "{sv}",
					       key, g_variant_new_string (value));
		}
	}
	return g_variant_new ("{sa{sv}}", device->priv->id, &builder);
}

/**
 * fu_device_get_metadata_as_variant:
 **/
GVariant *
fu_device_get_metadata_as_variant (FuDevice *device)
{
	GList *l;
	GVariantBuilder builder;
	const gchar *key;
	const gchar *value;
	_cleanup_list_free_ GList *keys = NULL;

	/* create an array with all the metadata in */
	g_variant_builder_init (&builder, G_VARIANT_TYPE_ARRAY);
	keys = g_hash_table_get_keys (device->priv->metadata);
	keys = g_list_sort (keys, (GCompareFunc) g_strcmp0);
	for (l = keys; l != NULL; l = l->next) {
		key = l->data;
		value = g_hash_table_lookup (device->priv->metadata, key);
		if (g_strcmp0 (value, "TRUE") == 0) {
			g_variant_builder_add (&builder, "{sv}",
					       key, g_variant_new_boolean (TRUE));
		} else {
			g_variant_builder_add (&builder, "{sv}",
					       key, g_variant_new_string (value));
		}
	}
	return g_variant_new ("(a{sv})", &builder);
}

/**
 * fu_device_set_metadata_from_iter:
 **/
void
fu_device_set_metadata_from_iter (FuDevice *device, GVariantIter *iter)
{
	GVariant *variant;
	const gchar *key;
	const gchar *type;

	while (g_variant_iter_next (iter, "{&sv}", &key, &variant)) {
		type = g_variant_get_type_string (variant);
		if (g_strcmp0 (type, "s") == 0) {
			fu_device_set_metadata (device, key,
						g_variant_get_string (variant, NULL));
		} else if (g_strcmp0 (type, "b") == 0) {
			fu_device_set_metadata (device, key, "TRUE");
		} else if (g_strcmp0 (type, "t") == 0) {
			_cleanup_free_ gchar *tmp = NULL;
			tmp = g_strdup_printf ("%" G_GUINT64_FORMAT,
					       g_variant_get_uint64 (variant));
			fu_device_set_metadata (device, key, tmp);
		} else {
			g_warning ("type=%s", type);
			fu_device_set_metadata (device, key, "???");
		}
		g_variant_unref (variant);
	}
}

/**
 * fu_device_get_property:
 **/
static void
fu_device_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	FuDevice *device = FU_DEVICE (object);
	FuDevicePrivate *priv = device->priv;

	switch (prop_id) {
	case PROP_ID:
		g_value_set_string (value, priv->id);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * fu_device_set_property:
 **/
static void
fu_device_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	FuDevice *device = FU_DEVICE (object);
	FuDevicePrivate *priv = device->priv;

	switch (prop_id) {
	case PROP_ID:
		g_free (priv->id);
		priv->id = g_strdup (g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * fu_device_class_init:
 **/
static void
fu_device_class_init (FuDeviceClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = fu_device_finalize;
	object_class->get_property = fu_device_get_property;
	object_class->set_property = fu_device_set_property;

	/**
	 * FuDevice:id:
	 */
	pspec = g_param_spec_string ("id", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_ID, pspec);

	g_type_class_add_private (klass, sizeof (FuDevicePrivate));
}

/**
 * fu_device_init:
 **/
static void
fu_device_init (FuDevice *device)
{
	device->priv = FU_DEVICE_GET_PRIVATE (device);
	device->priv->metadata = g_hash_table_new_full (g_str_hash, g_str_equal,
							g_free, g_free);
}

/**
 * fu_device_finalize:
 **/
static void
fu_device_finalize (GObject *object)
{
	FuDevice *device = FU_DEVICE (object);
	FuDevicePrivate *priv = device->priv;

	g_free (priv->id);
	g_hash_table_unref (priv->metadata);

	G_OBJECT_CLASS (fu_device_parent_class)->finalize (object);
}

/**
 * fu_device_new:
 **/
FuDevice *
fu_device_new (void)
{
	FuDevice *device;
	device = g_object_new (FU_TYPE_DEVICE, NULL);
	return FU_DEVICE (device);
}
