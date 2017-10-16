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

#include <string.h>
#include <appstream-glib.h>
#include <glib-object.h>
#include <gio/gio.h>

#include "fu-device-private.h"

/**
 * SECTION:fu-device
 * @short_description: a physical or logical device
 *
 * An object that represents a physical or logical device.
 *
 * See also: #FuDeviceLocker
 */

static void fu_device_finalize			 (GObject *object);

typedef struct {
	gchar				*equivalent_id;
	gchar				*version_new;
	gchar				*filename_pending;
	FuDevice			*alternate;
	GHashTable			*metadata;
} FuDevicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (FuDevice, fu_device, FWUPD_TYPE_DEVICE)
#define GET_PRIVATE(o) (fu_device_get_instance_private (o))

const gchar *
fu_device_get_equivalent_id (FuDevice *device)
{
	FuDevicePrivate *priv = GET_PRIVATE (device);
	g_return_val_if_fail (FU_IS_DEVICE (device), NULL);
	return priv->equivalent_id;
}

void
fu_device_set_equivalent_id (FuDevice *device, const gchar *equivalent_id)
{
	FuDevicePrivate *priv = GET_PRIVATE (device);
	g_return_if_fail (FU_IS_DEVICE (device));
	g_free (priv->equivalent_id);
	priv->equivalent_id = g_strdup (equivalent_id);
}

const gchar *
fu_device_get_version_new (FuDevice *device)
{
	FuDevicePrivate *priv = GET_PRIVATE (device);
	g_return_val_if_fail (FU_IS_DEVICE (device), NULL);
	return priv->version_new;
}

void
fu_device_set_version_new (FuDevice *device, const gchar *version_new)
{
	FuDevicePrivate *priv = GET_PRIVATE (device);
	g_return_if_fail (FU_IS_DEVICE (device));
	g_free (priv->version_new);
	priv->version_new = g_strdup (version_new);
}

const gchar *
fu_device_get_filename_pending (FuDevice *device)
{
	FuDevicePrivate *priv = GET_PRIVATE (device);
	g_return_val_if_fail (FU_IS_DEVICE (device), NULL);
	return priv->filename_pending;
}

void
fu_device_set_filename_pending (FuDevice *device, const gchar *filename_pending)
{
	FuDevicePrivate *priv = GET_PRIVATE (device);
	g_return_if_fail (FU_IS_DEVICE (device));
	g_free (priv->filename_pending);
	priv->filename_pending = g_strdup (filename_pending);
}

/**
 * fu_device_get_alternate:
 * @device: A #FuDevice
 *
 * Gets any alternate device. An alternate device may be linked to the primary
 * device in some way.
 *
 * Returns: (transfer none): a #FuDevice or %NULL
 *
 * Since: 0.7.2
 **/
FuDevice *
fu_device_get_alternate (FuDevice *device)
{
	FuDevicePrivate *priv = GET_PRIVATE (device);
	g_return_val_if_fail (FU_IS_DEVICE (device), NULL);
	return priv->alternate;
}

/**
 * fu_device_set_alternate:
 * @device: A #FuDevice
 * @alternate: Another #FuDevice
 *
 * Sets any alternate device. An alternate device may be linked to the primary
 * device in some way.
 *
 * Since: 0.7.2
 **/
void
fu_device_set_alternate (FuDevice *device, FuDevice *alternate)
{
	FuDevicePrivate *priv = GET_PRIVATE (device);
	g_return_if_fail (FU_IS_DEVICE (device));
	g_set_object (&priv->alternate, alternate);
}

/**
 * fu_device_add_guid:
 * @device: A #FuDevice
 * @guid: A GUID, e.g. `2082b5e0-7a64-478a-b1b2-e3404fab6dad`
 *
 * Adds a GUID to the device. If the @guid argument is not a valid GUID then it
 * is converted to a GUID using as_utils_guid_from_string().
 *
 * Since: 0.7.2
 **/
void
fu_device_add_guid (FuDevice *device, const gchar *guid)
{
	/* make valid */
	if (!as_utils_guid_is_valid (guid)) {
		g_autofree gchar *tmp = as_utils_guid_from_string (guid);
		g_debug ("using %s for %s", tmp, guid);
		fwupd_device_add_guid (FWUPD_DEVICE (device), tmp);
		return;
	}

	/* already valid */
	fwupd_device_add_guid (FWUPD_DEVICE (device), guid);
}

/**
 * fu_device_get_metadata:
 * @device: A #FuDevice
 * @key: the key
 *
 * Gets an item of metadata from the device.
 *
 * Returns: a string value, or %NULL for unfound.
 *
 * Since: 0.1.0
 **/
const gchar *
fu_device_get_metadata (FuDevice *device, const gchar *key)
{
	FuDevicePrivate *priv = GET_PRIVATE (device);
	g_return_val_if_fail (FU_IS_DEVICE (device), NULL);
	g_return_val_if_fail (key != NULL, NULL);
	return g_hash_table_lookup (priv->metadata, key);
}

/**
 * fu_device_get_metadata_boolean:
 * @device: A #FuDevice
 * @key: the key
 *
 * Gets an item of metadata from the device.
 *
 * Returns: a boolean value, or %FALSE for unfound or failure to parse.
 *
 * Since: 0.9.7
 **/
gboolean
fu_device_get_metadata_boolean (FuDevice *device, const gchar *key)
{
	FuDevicePrivate *priv = GET_PRIVATE (device);
	const gchar *tmp;
	g_return_val_if_fail (FU_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);
	tmp = g_hash_table_lookup (priv->metadata, key);
	if (tmp == NULL)
		return FALSE;
	return g_strcmp0 (tmp, "true") == 0;
}

/**
 * fu_device_get_metadata_integer:
 * @device: A #FuDevice
 * @key: the key
 *
 * Gets an item of metadata from the device.
 *
 * Returns: a string value, or %G_MAXUINT for unfound or failure to parse.
 *
 * Since: 0.9.7
 **/
guint
fu_device_get_metadata_integer (FuDevice *device, const gchar *key)
{
	FuDevicePrivate *priv = GET_PRIVATE (device);
	const gchar *tmp;
	gchar *endptr = NULL;
	guint64 val;

	g_return_val_if_fail (FU_IS_DEVICE (device), G_MAXUINT);
	g_return_val_if_fail (key != NULL, G_MAXUINT);

	tmp = g_hash_table_lookup (priv->metadata, key);
	if (tmp == NULL)
		return G_MAXUINT;
	val = g_ascii_strtoull (tmp, &endptr, 10);
	if (endptr != NULL && endptr[0] != '\0')
		return G_MAXUINT;
	if (val > G_MAXUINT)
		return G_MAXUINT;
	return (guint) val;
}

/**
 * fu_device_set_metadata:
 * @device: A #FuDevice
 * @key: the key
 * @value: the string value
 *
 * Sets an item of metadata on the device.
 *
 * Since: 0.1.0
 **/
void
fu_device_set_metadata (FuDevice *device, const gchar *key, const gchar *value)
{
	FuDevicePrivate *priv = GET_PRIVATE (device);
	g_return_if_fail (FU_IS_DEVICE (device));
	g_return_if_fail (key != NULL);
	g_return_if_fail (value != NULL);
	g_hash_table_insert (priv->metadata, g_strdup (key), g_strdup (value));
}

/**
 * fu_device_set_metadata_boolean:
 * @device: A #FuDevice
 * @key: the key
 * @value: the boolean value
 *
 * Sets an item of metadata on the device. When @value is set to %TRUE
 * the actual stored value is "true".
 *
 * Since: 0.9.7
 **/
void
fu_device_set_metadata_boolean (FuDevice *device, const gchar *key, gboolean value)
{
	fu_device_set_metadata (device, key, value ? "true" : "false");
}

/**
 * fu_device_set_metadata_integer:
 * @device: A #FuDevice
 * @key: the key
 * @value: the unsigned integer value
 *
 * Sets an item of metadata on the device. The integer is stored as a
 * base-10 string internally.
 *
 * Since: 0.9.7
 **/
void
fu_device_set_metadata_integer (FuDevice *device, const gchar *key, guint value)
{
	g_autofree gchar *tmp = g_strdup_printf ("%u", value);
	fu_device_set_metadata (device, key, tmp);
}

/**
 * fu_device_set_name:
 * @device: A #FuDevice
 * @value: a device name
 *
 * Sets the name on the device. Any invalid parts will be converted or removed.
 *
 * Since: 0.7.1
 **/
void
fu_device_set_name (FuDevice *device, const gchar *value)
{
	g_autoptr(GString) new = g_string_new (value);
	g_strdelimit (new->str, "_", ' ');
	as_utils_string_replace (new, "(TM)", "™");
	fwupd_device_set_name (FWUPD_DEVICE (device), new->str);
}

static void
fwupd_pad_kv_str (GString *str, const gchar *key, const gchar *value)
{
	/* ignore */
	if (key == NULL || value == NULL)
		return;
	g_string_append_printf (str, "  %s: ", key);
	for (gsize i = strlen (key); i < 20; i++)
		g_string_append (str, " ");
	g_string_append_printf (str, "%s\n", value);
}

/**
 * fu_device_to_string:
 * @device: A #FuDevice
 *
 * This allows us to easily print the FwupdDevice, the FwupdRelease and the
 * daemon-specific metadata.
 *
 * Returns: a string value, or %NULL for invalid.
 *
 * Since: 0.9.8
 **/
gchar *
fu_device_to_string (FuDevice *device)
{
	FuDevicePrivate *priv = GET_PRIVATE (device);
	GString *str = g_string_new ("");
	g_autofree gchar *tmp = NULL;
	g_autoptr(GList) keys = NULL;

	g_return_val_if_fail (FU_IS_DEVICE (device), NULL);

	tmp = fwupd_device_to_string (FWUPD_DEVICE (device));
	if (tmp != NULL && tmp[0] != '\0')
		g_string_append (str, tmp);
	if (priv->equivalent_id != NULL)
		fwupd_pad_kv_str (str, "EquivalentId", priv->equivalent_id);
	if (priv->filename_pending != NULL)
		fwupd_pad_kv_str (str, "FilenamePending", priv->filename_pending);
	if (priv->version_new != NULL)
		fwupd_pad_kv_str (str, "VersionNew", priv->version_new);
	keys = g_hash_table_get_keys (priv->metadata);
	for (GList *l = keys; l != NULL; l = l->next) {
		const gchar *key = l->data;
		const gchar *value = g_hash_table_lookup (priv->metadata, key);
		fwupd_pad_kv_str (str, key, value);
	}
	return g_string_free (str, FALSE);
}

static void
fu_device_class_init (FuDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = fu_device_finalize;
}

static void
fu_device_init (FuDevice *device)
{
	FuDevicePrivate *priv = GET_PRIVATE (device);
	priv->metadata = g_hash_table_new_full (g_str_hash, g_str_equal,
						g_free, g_free);
}

static void
fu_device_finalize (GObject *object)
{
	FuDevice *device = FU_DEVICE (object);
	FuDevicePrivate *priv = GET_PRIVATE (device);

	if (priv->alternate != NULL)
		g_object_unref (priv->alternate);
	g_hash_table_unref (priv->metadata);
	g_free (priv->equivalent_id);
	g_free (priv->version_new);
	g_free (priv->filename_pending);

	G_OBJECT_CLASS (fu_device_parent_class)->finalize (object);
}

FuDevice *
fu_device_new (void)
{
	FuDevice *device;
	device = g_object_new (FU_TYPE_DEVICE, NULL);
	return FU_DEVICE (device);
}
