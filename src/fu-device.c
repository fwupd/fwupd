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
	FuQuirks			*quirks;
	GHashTable			*metadata;
	guint				 remove_delay;	/* ms */
	FwupdStatus			 status;
	guint				 progress;
} FuDevicePrivate;

enum {
	PROP_0,
	PROP_STATUS,
	PROP_PROGRESS,
	PROP_PLATFORM_ID,
	PROP_QUIRKS,
	PROP_LAST
};

G_DEFINE_TYPE_WITH_PRIVATE (FuDevice, fu_device, FWUPD_TYPE_DEVICE)
#define GET_PRIVATE(o) (fu_device_get_instance_private (o))

static void
fu_device_get_property (GObject *object, guint prop_id,
			GValue *value, GParamSpec *pspec)
{
	FuDevice *device = FU_DEVICE (object);
	FuDevicePrivate *priv = GET_PRIVATE (device);
	switch (prop_id) {
	case PROP_STATUS:
		g_value_set_uint (value, priv->status);
		break;
	case PROP_PROGRESS:
		g_value_set_uint (value, priv->progress);
		break;
	case PROP_PLATFORM_ID:
		g_value_set_string (value, fu_device_get_platform_id (device));
		break;
	case PROP_QUIRKS:
		g_value_set_object (value, priv->quirks);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
fu_device_set_property (GObject *object, guint prop_id,
			const GValue *value, GParamSpec *pspec)
{
	FuDevice *device = FU_DEVICE (object);
	switch (prop_id) {
	case PROP_STATUS:
		fu_device_set_status (device, g_value_get_uint (value));
		break;
	case PROP_PROGRESS:
		fu_device_set_progress (device, g_value_get_uint (value));
		break;
	case PROP_PLATFORM_ID:
		fu_device_set_platform_id (device, g_value_get_string (value));
		break;
	case PROP_QUIRKS:
		fu_device_set_quirks (device, g_value_get_object (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

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

	/* overwriting? */
	if (g_strcmp0 (value, fu_device_get_name (device)) == 0) {
		g_warning ("device %s overwriting same name value: %s",
			   fu_device_get_id (device), value);
		return;
	}

	/* changing */
	if (fu_device_get_name (device) != NULL) {
		g_debug ("device %s overwriting name value: %s->%s",
			 fu_device_get_id (device),
			 fu_device_get_name (device),
			 value);
	}

	g_strdelimit (new->str, "_", ' ');
	as_utils_string_replace (new, "(TM)", "™");
	fwupd_device_set_name (FWUPD_DEVICE (device), new->str);
}

/**
 * fu_device_set_id:
 * @device: A #FuDevice
 * @id: a string, e.g. `tbt-port1`
 *
 * Sets the ID on the device. The ID should represent the *connection* of the
 * device, so that any similar device plugged into a different slot will
 * have a different @id string.
 *
 * The @id will be converted to a SHA1 hash before the device is added to the
 * daemon, and plugins should not assume that the ID that is set here is the
 * same as what is returned by fu_device_get_id().
 *
 * Since: 0.7.1
 **/
void
fu_device_set_id (FuDevice *device, const gchar *id)
{
	g_autofree gchar *id_hash = NULL;
	g_return_if_fail (FU_IS_DEVICE (device));
	g_return_if_fail (id != NULL);
	id_hash = g_compute_checksum_for_string (G_CHECKSUM_SHA1, id, -1);
	g_debug ("using %s for %s", id_hash, id);
	fwupd_device_set_id (FWUPD_DEVICE (device), id_hash);
}

/**
 * fu_device_set_serial:
 * @device: A #FuDevice
 * @serial: a serial number string, e.g. `0000123`
 *
 * Sets the serial number for the device.
 *
 * Since: 1.0.3
 **/
void
fu_device_set_serial (FuDevice *device, const gchar *serial)
{
	g_return_if_fail (FU_IS_DEVICE (device));
	g_return_if_fail (serial != NULL);
	fu_device_set_metadata (device, "serial", serial);
}

/**
 * fu_device_get_serial:
 * @device: A #FuDevice
 *
 * Gets the serial number for the device.
 *
 * Returns: a string value, or %NULL if never set.
 *
 * Since: 1.0.3
 **/
const gchar *
fu_device_get_serial (FuDevice *device)
{
	g_return_val_if_fail (FU_IS_DEVICE (device), NULL);
	return fu_device_get_metadata (device, "serial");
}

/**
 * fu_device_set_plugin_hints:
 * @device: A #FuDevice
 * @plugin_hints: a string
 *
 * Sets the hint the the plugin from the quirk system that can be used to
 * do affect device matching. The actual string format is defined by the plugin.
 *
 * Since: 1.0.3
 **/
void
fu_device_set_plugin_hints (FuDevice *device, const gchar *plugin_hints)
{
	g_return_if_fail (FU_IS_DEVICE (device));
	g_return_if_fail (plugin_hints != NULL);
	fu_device_set_metadata (device, "PluginHints", plugin_hints);
}

/**
 * fu_device_get_plugin_hints:
 * @device: A #FuDevice
 *
 * Gets the plugin hint for the device from the quirk system.
 *
 * Returns: a string value, or %NULL if never set.
 *
 * Since: 1.0.3
 **/
const gchar *
fu_device_get_plugin_hints (FuDevice *device)
{
	g_return_val_if_fail (FU_IS_DEVICE (device), NULL);
	return fu_device_get_metadata (device, "PluginHints");
}

/**
 * fu_device_set_platform_id:
 * @device: A #FuDevice
 * @platform_id: a platform string, e.g. `/sys/devices/usb1/1-1/1-1.2`
 *
 * Sets the Platform ID on the device. If unset, the ID will automatically
 * be set using a hash of the @platform_id value.
 *
 * Since: 1.0.2
 **/
void
fu_device_set_platform_id (FuDevice *device, const gchar *platform_id)
{
	g_return_if_fail (FU_IS_DEVICE (device));
	g_return_if_fail (platform_id != NULL);

	/* automatically use this */
	if (fu_device_get_id (device) == NULL)
		fu_device_set_id (device, platform_id);
	fu_device_set_metadata (device, "platform-id", platform_id);
}

/**
 * fu_device_get_platform_id:
 * @device: A #FuDevice
 *
 * Gets the Platform ID set for the device, which represents the connection
 * string used to compare devices.
 *
 * Returns: a string value, or %NULL if never set.
 *
 * Since: 1.0.2
 **/
const gchar *
fu_device_get_platform_id (FuDevice *device)
{
	g_return_val_if_fail (FU_IS_DEVICE (device), NULL);
	return fu_device_get_metadata (device, "platform-id");
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
 * fu_device_get_remove_delay:
 * @device: A #FuDevice
 *
 * Returns the maximum delay expected when replugging the device going into
 * bootloader mode.
 *
 * Returns: time in milliseconds
 *
 * Since: 1.0.2
 **/
guint
fu_device_get_remove_delay (FuDevice *device)
{
	FuDevicePrivate *priv = GET_PRIVATE (device);
	g_return_val_if_fail (FU_IS_DEVICE (device), 0);
	return priv->remove_delay;
}

/**
 * fu_device_set_remove_delay:
 * @device: A #FuDevice
 * @remove_delay: the remove_delay value
 *
 * Sets the amount of time a device is allowed to return in bootloader mode.
 *
 * NOTE: this should be less than 3000ms for devices that just have to reset
 * and automatically re-enumerate, but significantly longer if it involves a
 * user removing a cable, pressing several buttons and removing a cable.
 * A suggested value for this would be 10,000ms.
 *
 * Since: 1.0.2
 **/
void
fu_device_set_remove_delay (FuDevice *device, guint remove_delay)
{
	FuDevicePrivate *priv = GET_PRIVATE (device);
	g_return_if_fail (FU_IS_DEVICE (device));
	priv->remove_delay = remove_delay;
}

/**
 * fu_device_get_status:
 * @device: A #FuDevice
 *
 * Returns what the device is currently doing.
 *
 * Returns: the status value, e.g. %FWUPD_STATUS_DEVICE_WRITE
 *
 * Since: 1.0.3
 **/
FwupdStatus
fu_device_get_status (FuDevice *device)
{
	FuDevicePrivate *priv = GET_PRIVATE (device);
	g_return_val_if_fail (FU_IS_DEVICE (device), 0);
	return priv->status;
}

/**
 * fu_device_set_status:
 * @device: A #FuDevice
 * @status: the status value, e.g. %FWUPD_STATUS_DEVICE_WRITE
 *
 * Sets what the device is currently doing.
 *
 * Since: 1.0.3
 **/
void
fu_device_set_status (FuDevice *device, FwupdStatus status)
{
	FuDevicePrivate *priv = GET_PRIVATE (device);
	g_return_if_fail (FU_IS_DEVICE (device));
	if (priv->status == status)
		return;
	priv->status = status;
	g_object_notify (G_OBJECT (device), "status");
}

/**
 * fu_device_get_progress:
 * @device: A #FuDevice
 *
 * Returns the progress completion.
 *
 * Returns: value in percent
 *
 * Since: 1.0.3
 **/
guint
fu_device_get_progress (FuDevice *device)
{
	FuDevicePrivate *priv = GET_PRIVATE (device);
	g_return_val_if_fail (FU_IS_DEVICE (device), 0);
	return priv->progress;
}

/**
 * fu_device_set_progress:
 * @device: A #FuDevice
 * @progress: the progress percentage value
 *
 * Sets the progress completion.
 *
 * Since: 1.0.3
 **/
void
fu_device_set_progress (FuDevice *device, guint progress)
{
	FuDevicePrivate *priv = GET_PRIVATE (device);
	g_return_if_fail (FU_IS_DEVICE (device));
	if (priv->progress == progress)
		return;
	priv->progress = progress;
	g_object_notify (G_OBJECT (device), "progress");
}

/**
 * fu_device_set_progress_full:
 * @device: A #FuDevice
 * @progress_done: the bytes already done
 * @progress_total: the total number of bytes
 *
 * Sets the progress completion using the raw progress values.
 *
 * Since: 1.0.3
 **/
void
fu_device_set_progress_full (FuDevice *device, gsize progress_done, gsize progress_total)
{
	gdouble percentage = 0.f;
	if (progress_total > 0)
		percentage = (100.f * (gdouble) progress_done) / (gdouble) progress_total;
	fu_device_set_progress (device, (guint) percentage);
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
	FuDeviceClass *klass = FU_DEVICE_GET_CLASS (device);
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

	/* subclassed */
	if (klass->to_string != NULL)
		klass->to_string (device, str);

	return g_string_free (str, FALSE);
}

/**
 * fu_device_set_quirks:
 * @device: A #FuDevice
 * @quirks: A #FuQuirks, or %NULL
 *
 * Sets the optional quirk information which may be useful to this device.
 * This is typically set after the #FuDevice has been created, but before
 * the device has been opened or probed.
 *
 * Since: 1.0.3
 **/
void
fu_device_set_quirks (FuDevice *device, FuQuirks *quirks)
{
	FuDevicePrivate *priv = GET_PRIVATE (device);
	g_return_if_fail (FU_IS_DEVICE (device));
	if (g_set_object (&priv->quirks, quirks))
		g_object_notify (G_OBJECT (device), "quirks");
}

/**
 * fu_device_get_quirks:
 * @device: A #FuDevice
 *
 * Gets the quirk information which may be useful to this device.
 *
 * Returns: (transfer none): the #FuQuirks object, or %NULL
 *
 * Since: 1.0.3
 **/
FuQuirks *
fu_device_get_quirks (FuDevice *device)
{
	FuDevicePrivate *priv = GET_PRIVATE (device);
	g_return_val_if_fail (FU_IS_DEVICE (device), NULL);
	return priv->quirks;
}

static void
fu_device_class_init (FuDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GParamSpec *pspec;
	object_class->finalize = fu_device_finalize;
	object_class->get_property = fu_device_get_property;
	object_class->set_property = fu_device_set_property;

	pspec = g_param_spec_uint ("status", NULL, NULL,
				   FWUPD_STATUS_UNKNOWN,
				   FWUPD_STATUS_LAST,
				   FWUPD_STATUS_UNKNOWN,
				   G_PARAM_READWRITE |
				   G_PARAM_STATIC_NAME);
	g_object_class_install_property (object_class, PROP_STATUS, pspec);

	pspec = g_param_spec_uint ("progress", NULL, NULL,
				   0, 100, 0,
				   G_PARAM_READWRITE |
				   G_PARAM_STATIC_NAME);
	g_object_class_install_property (object_class, PROP_PROGRESS, pspec);

	pspec = g_param_spec_string ("platform-id", NULL, NULL, NULL,
				     G_PARAM_READWRITE |
				     G_PARAM_STATIC_NAME);
	g_object_class_install_property (object_class, PROP_PLATFORM_ID, pspec);

	pspec = g_param_spec_object ("quirks", NULL, NULL,
				     FU_TYPE_QUIRKS,
				     G_PARAM_READWRITE |
				     G_PARAM_STATIC_NAME);
	g_object_class_install_property (object_class, PROP_QUIRKS, pspec);
}

static void
fu_device_init (FuDevice *device)
{
	FuDevicePrivate *priv = GET_PRIVATE (device);
	priv->status = FWUPD_STATUS_IDLE;
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
	if (priv->quirks != NULL)
		g_object_unref (priv->quirks);
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
