/*
 * Copyright (C) 2015-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>
#include <appstream-glib.h>
#include <glib-object.h>
#include <gio/gio.h>

#include "fu-common.h"
#include "fu-device-private.h"
#include "fwupd-device-private.h"

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
	gchar				*alternate_id;
	gchar				*equivalent_id;
	FuDevice			*alternate;
	FuDevice			*parent;	/* noref */
	FuQuirks			*quirks;
	GHashTable			*metadata;
	GPtrArray			*parent_guids;
	GPtrArray			*children;
	guint				 remove_delay;	/* ms */
	FwupdStatus			 status;
	guint				 progress;
	guint				 order;
	guint				 priority;
	gboolean			 done_probe;
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

/**
 * fu_device_get_order:
 * @device: a #FuPlugin
 *
 * Gets the device order, where higher numbers are installed after lower
 * numbers.
 *
 * Returns: the integer value
 *
 * Since: 1.0.8
 **/
guint
fu_device_get_order (FuDevice *device)
{
	FuDevicePrivate *priv = fu_device_get_instance_private (device);
	return priv->order;
}

/**
 * fu_device_set_order:
 * @device: a #FuDevice
 * @order: a integer value
 *
 * Sets the device order, where higher numbers are installed after lower
 * numbers.
 *
 * Since: 1.0.8
 **/
void
fu_device_set_order (FuDevice *device, guint order)
{
	FuDevicePrivate *priv = fu_device_get_instance_private (device);
	priv->order = order;
}

/**
 * fu_device_get_priority:
 * @device: a #FuPlugin
 *
 * Gets the device priority, where higher numbers are better.
 *
 * Returns: the integer value
 *
 * Since: 1.1.1
 **/
guint
fu_device_get_priority (FuDevice *device)
{
	FuDevicePrivate *priv = fu_device_get_instance_private (device);
	return priv->priority;
}

/**
 * fu_device_set_priority:
 * @device: a #FuDevice
 * @priority: a integer value
 *
 * Sets the device priority, where higher numbers are better.
 *
 * Since: 1.1.1
 **/
void
fu_device_set_priority (FuDevice *device, guint priority)
{
	FuDevicePrivate *priv = fu_device_get_instance_private (device);
	priv->priority = priority;
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

/**
 * fu_device_get_alternate:
 * @device: A #FuDevice
 *
 * Gets any alternate device ID. An alternate device may be linked to the primary
 * device in some way.
 *
 * Returns: (transfer none): a #FuDevice or %NULL
 *
 * Since: 1.1.0
 **/
const gchar *
fu_device_get_alternate_id (FuDevice *device)
{
	FuDevicePrivate *priv = GET_PRIVATE (device);
	g_return_val_if_fail (FU_IS_DEVICE (device), NULL);
	return priv->alternate_id;
}

/**
 * fu_device_set_alternate:
 * @device: A #FuDevice
 * @alternate: Another #FuDevice
 *
 * Sets any alternate device ID. An alternate device may be linked to the primary
 * device in some way.
 *
 * Since: 1.1.0
 **/
void
fu_device_set_alternate_id (FuDevice *device, const gchar *alternate_id)
{
	FuDevicePrivate *priv = GET_PRIVATE (device);
	g_return_if_fail (FU_IS_DEVICE (device));
	g_free (priv->alternate_id);
	priv->alternate_id = g_strdup (alternate_id);
}

/**
 * fu_device_get_alternate:
 * @device: A #FuDevice
 *
 * Gets any alternate device. An alternate device may be linked to the primary
 * device in some way.
 *
 * The alternate object will be matched from the ID set in fu_device_set_alternate_id()
 * and will be assigned by the daemon. This means if the ID is not found as an
 * added device, then this function will return %NULL.
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
 * This function is only usable by the daemon, not directly from plugins.
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
 * fu_device_get_parent:
 * @device: A #FuDevice
 *
 * Gets any parent device. An parent device is logically "above" the current
 * device and this may be reflected in client tools.
 *
 * This information also allows the plugin to optionally verify the parent
 * device, for instance checking the parent device firmware version.
 *
 * The parent object is not refcounted and if destroyed this function will then
 * return %NULL.
 *
 * Returns: (transfer none): a #FuDevice or %NULL
 *
 * Since: 1.0.8
 **/
FuDevice *
fu_device_get_parent (FuDevice *device)
{
	FuDevicePrivate *priv = GET_PRIVATE (device);
	g_return_val_if_fail (FU_IS_DEVICE (device), NULL);
	return priv->parent;
}

static void
fu_device_set_parent (FuDevice *device, FuDevice *parent)
{
	FuDevicePrivate *priv = GET_PRIVATE (device);

	g_object_add_weak_pointer (G_OBJECT (parent), (gpointer *) &priv->parent);
	priv->parent = parent;

	/* this is what goes over D-Bus */
	fwupd_device_set_parent_id (FWUPD_DEVICE (device),
				    device != NULL ? fu_device_get_id (parent) : NULL);
}

/**
 * fu_device_get_children:
 * @device: A #FuDevice
 *
 * Gets any child devices. A child device is logically "below" the current
 * device and this may be reflected in client tools.
 *
 * Returns: (transfer none) (element-type FuDevice): child devices
 *
 * Since: 1.0.8
 **/
GPtrArray *
fu_device_get_children (FuDevice *device)
{
	FuDevicePrivate *priv = GET_PRIVATE (device);
	g_return_val_if_fail (FU_IS_DEVICE (device), NULL);
	return priv->children;
}

/**
 * fu_device_add_child:
 * @device: A #FuDevice
 * @child: Another #FuDevice
 *
 * Sets any child device. An child device is logically linked to the primary
 * device in some way.
 *
 * Since: 1.0.8
 **/
void
fu_device_add_child (FuDevice *device, FuDevice *child)
{
	FuDevicePrivate *priv = GET_PRIVATE (device);
	g_return_if_fail (FU_IS_DEVICE (device));
	g_return_if_fail (FU_IS_DEVICE (child));

	/* add if the child does not already exist */
	for (guint i = 0; i < priv->children->len; i++) {
		FuDevice *devtmp = g_ptr_array_index (priv->children, i);
		if (devtmp == child)
			return;
	}
	g_ptr_array_add (priv->children, g_object_ref (child));

	/* copy from main device if unset */
	if (fu_device_get_vendor (child) == NULL)
		fu_device_set_vendor (child, fu_device_get_vendor (device));
	if (fu_device_get_vendor_id (child) == NULL)
		fu_device_set_vendor_id (child, fu_device_get_vendor_id (device));

	/* ensure the parent is also set on the child */
	fu_device_set_parent (child, device);

	/* order devices so they are updated in the correct sequence */
	if (fu_device_has_flag (child, FWUPD_DEVICE_FLAG_INSTALL_PARENT_FIRST)) {
		if (priv->order >= fu_device_get_order (child))
			fu_device_set_order (child, priv->order + 1);
	} else {
		if (priv->order <= fu_device_get_order (child))
			priv->order = fu_device_get_order (child) + 1;
	}
}

/**
 * fu_device_get_parent_guids:
 * @device: A #FuDevice
 *
 * Gets any parent device GUIDs. If a device is added to the daemon that matches
 * any GUIDs added from fu_device_add_parent_guid() then this device is marked the parent of @device.
 *
 * Returns: (transfer none) (element-type utf8): a list of GUIDs
 *
 * Since: 1.0.8
 **/
GPtrArray *
fu_device_get_parent_guids (FuDevice *device)
{
	FuDevicePrivate *priv = GET_PRIVATE (device);
	g_return_val_if_fail (FU_IS_DEVICE (device), NULL);
	return priv->parent_guids;
}

/**
 * fu_device_has_parent_guid:
 * @device: A #FuDevice
 * @guid: a GUID
 *
 * Searches the list of parent GUIDs for a string match.
 *
 * Returns: %TRUE if the parent GUID exists
 *
 * Since: 1.0.8
 **/
gboolean
fu_device_has_parent_guid (FuDevice *device, const gchar *guid)
{
	FuDevicePrivate *priv = GET_PRIVATE (device);
	g_return_val_if_fail (FU_IS_DEVICE (device), FALSE);
	for (guint i = 0; i < priv->parent_guids->len; i++) {
		const gchar *guid_tmp = g_ptr_array_index (priv->parent_guids, i);
		if (g_strcmp0 (guid_tmp, guid) == 0)
			return TRUE;
	}
	return FALSE;
}

/**
 * fu_device_add_parent_guid:
 * @device: A #FuDevice
 * @guid: a GUID
 *
 * Sets any parent device using a GUID. An parent device is logically linked to
 * the primary device in some way and can be added before or after @device.
 *
 * The GUIDs are searched in order, and so the order of adding GUIDs may be
 * important if more than one parent device might match.
 *
 * Since: 1.0.8
 **/
void
fu_device_add_parent_guid (FuDevice *device, const gchar *guid)
{
	FuDevicePrivate *priv = GET_PRIVATE (device);
	g_return_if_fail (FU_IS_DEVICE (device));
	g_return_if_fail (guid != NULL);

	/* make valid */
	if (!as_utils_guid_is_valid (guid)) {
		g_autofree gchar *tmp = as_utils_guid_from_string (guid);
		if (fu_device_has_parent_guid (device, tmp))
			return;
		g_debug ("using %s for %s", tmp, guid);
		g_ptr_array_add (priv->parent_guids, g_steal_pointer (&tmp));
		return;
	}

	/* already valid */
	if (fu_device_has_parent_guid (device, guid))
		return;
	g_ptr_array_add (priv->parent_guids, g_strdup (guid));
}

static void
fu_device_add_guid_quirks (FuDevice *device, const gchar *guid)
{
	FuDevicePrivate *priv = GET_PRIVATE (device);
	const gchar *tmp;

	/* not set */
	if (priv->quirks == NULL)
		return;

	/* flags */
	tmp = fu_quirks_lookup_by_guid (priv->quirks, guid, FU_QUIRKS_FLAGS);
	if (tmp != NULL)
		fu_device_set_custom_flags (device, tmp);

	/* name */
	tmp = fu_quirks_lookup_by_guid (priv->quirks, guid, FU_QUIRKS_NAME);
	if (tmp != NULL)
		fu_device_set_name (device, tmp);

	/* summary */
	tmp = fu_quirks_lookup_by_guid (priv->quirks, guid, FU_QUIRKS_SUMMARY);
	if (tmp != NULL)
		fu_device_set_summary (device, tmp);

	/* vendor */
	tmp = fu_quirks_lookup_by_guid (priv->quirks, guid, FU_QUIRKS_VENDOR);
	if (tmp != NULL)
		fu_device_set_vendor (device, tmp);

	/* version */
	tmp = fu_quirks_lookup_by_guid (priv->quirks, guid, FU_QUIRKS_VERSION);
	if (tmp != NULL)
		fu_device_set_version (device, tmp);

	/* icon */
	tmp = fu_quirks_lookup_by_guid (priv->quirks, guid, FU_QUIRKS_ICON);
	if (tmp != NULL)
		fu_device_add_icon (device, tmp);

	/* GUID */
	tmp = fu_quirks_lookup_by_guid (priv->quirks, guid, FU_QUIRKS_GUID);
	if (tmp != NULL)
		fu_device_add_guid (device, tmp);
}

static void
fu_device_add_guid_safe (FuDevice *device, const gchar *guid)
{
	/* add the device GUID before adding additional GUIDs from quirks
	 * to ensure the bootloader GUID is listed after the runtime GUID */
	fwupd_device_add_guid (FWUPD_DEVICE (device), guid);
	fu_device_add_guid_quirks (device, guid);
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
		fu_device_add_guid_safe (device, tmp);
		return;
	}

	/* already valid */
	fu_device_add_guid_safe (device, guid);
}

/**
 * fu_device_get_guids_as_str:
 * @device: A #FuDevice
 *
 * Gets the device GUIDs as a joined string, which may be useful for error
 * messages.
 *
 * Returns: a string, which may be empty length but not %NULL
 *
 * Since: 1.0.8
 **/
gchar *
fu_device_get_guids_as_str (FuDevice *device)
{
	GPtrArray *guids = fu_device_get_guids (device);
	g_autofree gchar **tmp = g_new0 (gchar *, guids->len + 1);
	for (guint i = 0; i < guids->len; i++)
		tmp[i] = g_ptr_array_index (guids, i);
	return g_strjoinv (",", tmp);
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

static void
fu_device_set_custom_flag (FuDevice *device, const gchar *hint)
{
	FwupdDeviceFlags flag;

	/* is this a known device flag */
	flag = fwupd_device_flag_from_string (hint);
	if (flag == FWUPD_DEVICE_FLAG_UNKNOWN)
		return;

	/* being both a bootloader and requiring a bootloader is invalid */
	if (flag == FWUPD_DEVICE_FLAG_NONE ||
	    flag == FWUPD_DEVICE_FLAG_NEEDS_BOOTLOADER) {
		fu_device_remove_flag (device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	}
	if (flag == FWUPD_DEVICE_FLAG_NONE ||
	    flag == FWUPD_DEVICE_FLAG_IS_BOOTLOADER) {
		fu_device_remove_flag (device, FWUPD_DEVICE_FLAG_NEEDS_BOOTLOADER);
	}

	/* none is not used as an "exported" flag */
	if (flag != FWUPD_DEVICE_FLAG_NONE)
		fu_device_add_flag (device, flag);
}

/**
 * fu_device_set_custom_flags:
 * @device: A #FuDevice
 * @custom_flags: a string
 *
 * Sets the custom flags from the quirk system that can be used to
 * affect device matching. The actual string format is defined by the plugin.
 *
 * Since: 1.1.0
 **/
void
fu_device_set_custom_flags (FuDevice *device, const gchar *custom_flags)
{
	g_return_if_fail (FU_IS_DEVICE (device));
	g_return_if_fail (custom_flags != NULL);

	/* display what was set when converting to a string */
	fu_device_set_metadata (device, "CustomFlags", custom_flags);

	/* look for any standard FwupdDeviceFlags */
	if (custom_flags != NULL) {
		g_auto(GStrv) hints = g_strsplit (custom_flags, ",", -1);
		for (guint i = 0; hints[i] != NULL; i++)
			fu_device_set_custom_flag (device, hints[i]);
	}
}

/**
 * fu_device_get_custom_flags:
 * @device: A #FuDevice
 *
 * Gets the custom flags for the device from the quirk system.
 *
 * Returns: a string value, or %NULL if never set.
 *
 * Since: 1.1.0
 **/
const gchar *
fu_device_get_custom_flags (FuDevice *device)
{
	g_return_val_if_fail (FU_IS_DEVICE (device), NULL);
	return fu_device_get_metadata (device, "CustomFlags");
}

/**
 * fu_device_has_custom_flag:
 * @device: A #FuDevice
 * @hint: A string, e.g. "bootloader"
 *
 * Checks if the custom flag exists for the device from the quirk system.
 *
 * It may be more efficient to call fu_device_get_custom_flags() and split the
 * string locally if checking for lots of different flags.
 *
 * Returns: %TRUE if the hint exists
 *
 * Since: 1.1.0
 **/
gboolean
fu_device_has_custom_flag (FuDevice *device, const gchar *hint)
{
	const gchar *hint_str;
	g_auto(GStrv) hints = NULL;

	g_return_val_if_fail (FU_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (hint != NULL, FALSE);

	/* no hint is perfectly valid */
	hint_str = fu_device_get_custom_flags (device);
	if (hint_str == NULL)
		return FALSE;
	hints = g_strsplit (hint_str, ",", -1);
	return g_strv_contains ((const gchar * const *) hints, hint);
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
	if (fu_device_get_id (device) == NULL) {
		if (fu_device_get_guid_default (device) != NULL) {
			g_autofree gchar *id_guid = NULL;
			id_guid = g_strdup_printf ("%s:%s", platform_id,
						   fu_device_get_guid_default (device));
			fu_device_set_id (device, id_guid);
		} else {
			fu_device_set_id (device, platform_id);
		}
	}
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
	if (priv->alternate_id != NULL)
		fwupd_pad_kv_str (str, "AlternateId", priv->alternate_id);
	if (priv->equivalent_id != NULL)
		fwupd_pad_kv_str (str, "EquivalentId", priv->equivalent_id);
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

/**
 * fu_device_get_release_default:
 * @device: A #FuDevice
 *
 * Gets the default release for the device, creating one if not found.
 *
 * Returns: (transfer none): the #FwupdRelease object
 *
 * Since: 1.0.5
 **/
FwupdRelease *
fu_device_get_release_default (FuDevice *device)
{
	g_autoptr(FwupdRelease) rel = NULL;
	if (fwupd_device_get_release_default (FWUPD_DEVICE (device)) != NULL)
		return fwupd_device_get_release_default (FWUPD_DEVICE (device));
	rel = fwupd_release_new ();
	fwupd_device_add_release (FWUPD_DEVICE (device), rel);
	return rel;
}

/**
 * fu_device_write_firmware:
 * @device: A #FuDevice
 * @fw: A #GBytes
 * @error: A #GError
 *
 * Writes firmware to the device by calling a plugin-specific vfunc.
 *
 * Returns: %TRUE on success
 *
 * Since: 1.0.8
 **/
gboolean
fu_device_write_firmware (FuDevice *device, GBytes *fw, GError **error)
{
	FuDeviceClass *klass = FU_DEVICE_GET_CLASS (device);

	g_return_val_if_fail (FU_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* no plugin-specific method */
	if (klass->write_firmware == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "not supported");
		return FALSE;
	}

	/* call vfunc */
	return klass->write_firmware (device, fw, error);
}

/**
 * fu_device_read_firmware:
 * @device: A #FuDevice
 * @error: A #GError
 *
 * Reads firmware from the device by calling a plugin-specific vfunc.
 *
 * Returns: (transfer full): A #GBytes, or %NULL for error
 *
 * Since: 1.0.8
 **/
GBytes *
fu_device_read_firmware (FuDevice *device, GError **error)
{
	FuDeviceClass *klass = FU_DEVICE_GET_CLASS (device);

	g_return_val_if_fail (FU_IS_DEVICE (device), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* no plugin-specific method */
	if (klass->read_firmware == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "not supported");
		return NULL;
	}

	/* call vfunc */
	return klass->read_firmware (device, error);
}

/**
 * fu_device_detach:
 * @device: A #FuDevice
 * @error: A #GError
 *
 * Detaches a device from the application into bootloader mode.
 *
 * Returns: %TRUE on success
 *
 * Since: 1.0.8
 **/
gboolean
fu_device_detach (FuDevice *device, GError **error)
{
	FuDeviceClass *klass = FU_DEVICE_GET_CLASS (device);

	g_return_val_if_fail (FU_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* no plugin-specific method */
	if (klass->detach == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "not supported");
		return FALSE;
	}

	/* call vfunc */
	return klass->detach (device, error);
}

/**
 * fu_device_attach:
 * @device: A #FuDevice
 * @error: A #GError
 *
 * Attaches a device from the bootloader into application mode.
 *
 * Returns: %TRUE on success
 *
 * Since: 1.0.8
 **/
gboolean
fu_device_attach (FuDevice *device, GError **error)
{
	FuDeviceClass *klass = FU_DEVICE_GET_CLASS (device);

	g_return_val_if_fail (FU_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* no plugin-specific method */
	if (klass->attach == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "not supported");
		return FALSE;
	}

	/* call vfunc */
	return klass->attach (device, error);
}

/**
 * fu_device_open:
 * @device: A #FuDevice
 * @error: A #GError, or %NULL
 *
 * Opens a device, optionally running a object-specific vfunc.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.1.2
 **/
gboolean
fu_device_open (FuDevice *device, GError **error)
{
	FuDeviceClass *klass = FU_DEVICE_GET_CLASS (device);

	g_return_val_if_fail (FU_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* probe */
	if (!fu_device_probe (device, error))
		return FALSE;

	/* subclassed */
	if (klass->open != NULL) {
		if (!klass->open (device, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

/**
 * fu_device_close:
 * @device: A #FuDevice
 * @error: A #GError, or %NULL
 *
 * Closes a device, optionally running a object-specific vfunc.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.1.2
 **/
gboolean
fu_device_close (FuDevice *device, GError **error)
{
	FuDeviceClass *klass = FU_DEVICE_GET_CLASS (device);

	g_return_val_if_fail (FU_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* subclassed */
	if (klass->close != NULL) {
		if (!klass->close (device, error))
			return FALSE;
	}
	return TRUE;
}

/**
 * fu_device_probe:
 * @device: A #FuDevice
 * @error: A #GError, or %NULL
 *
 * Probes a device, setting parameters on the object that does not need
 * the device open or the interface claimed.
 * If the device is not compatible then an error should be returned.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.1.2
 **/
gboolean
fu_device_probe (FuDevice *device, GError **error)
{
	FuDevicePrivate *priv = GET_PRIVATE (device);
	FuDeviceClass *klass = FU_DEVICE_GET_CLASS (device);

	g_return_val_if_fail (FU_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* already done */
	if (priv->done_probe)
		return TRUE;

	/* subclassed */
	if (klass->probe != NULL) {
		if (!klass->probe (device, error))
			return FALSE;
	}
	priv->done_probe = TRUE;
	return TRUE;
}

/**
 * fu_device_probe_invalidate:
 * @device: A #FuDevice
 *
 * Normally when calling fu_device_probe() multiple times it is only done once.
 * Calling this method causes the next fu_device_probe() call to actually
 * probe the hardware.
 *
 * This should be done in case the backing device has changed, for instance if
 * a USB device has been replugged.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.1.2
 **/
void
fu_device_probe_invalidate (FuDevice *device)
{
	FuDevicePrivate *priv = GET_PRIVATE (device);
	g_return_if_fail (FU_IS_DEVICE (device));
	priv->done_probe = FALSE;
}

/**
 * fu_device_incorporate:
 * @device: A #FuDevice
 * @donor: Another #FuDevice
 *
 * Copy all properties from the donor object if they have not already been set.
 *
 * Since: 1.1.0
 **/
void
fu_device_incorporate (FuDevice *self, FuDevice *donor)
{
	FuDevicePrivate *priv = GET_PRIVATE (self);
	FuDevicePrivate *priv_donor = GET_PRIVATE (donor);
	g_autoptr(GList) metadata_keys = NULL;

	/* copy from donor FuDevice if has not already been set */
	if (priv->alternate_id == NULL)
		fu_device_set_alternate_id (self, fu_device_get_alternate_id (donor));
	if (priv->equivalent_id == NULL)
		fu_device_set_equivalent_id (self, fu_device_get_equivalent_id (donor));
	if (priv->quirks == NULL)
		fu_device_set_quirks (self, fu_device_get_quirks (donor));
	metadata_keys = g_hash_table_get_keys (priv_donor->metadata);
	for (GList *l = metadata_keys; l != NULL; l = l->next) {
		const gchar *key = l->data;
		if (g_hash_table_lookup (priv->metadata, key) == NULL) {
			const gchar *value = g_hash_table_lookup (priv_donor->metadata, key);
			fu_device_set_metadata (self, key, value);
		}
	}

	/* now the base class, where all the interesting bits are */
	fwupd_device_incorporate (FWUPD_DEVICE (self), FWUPD_DEVICE (donor));
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
	priv->children = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	priv->parent_guids = g_ptr_array_new_with_free_func (g_free);
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
	if (priv->parent != NULL)
		g_object_remove_weak_pointer (G_OBJECT (priv->parent), (gpointer *) &priv->parent);
	if (priv->quirks != NULL)
		g_object_unref (priv->quirks);
	g_hash_table_unref (priv->metadata);
	g_ptr_array_unref (priv->children);
	g_ptr_array_unref (priv->parent_guids);
	g_free (priv->alternate_id);
	g_free (priv->equivalent_id);

	G_OBJECT_CLASS (fu_device_parent_class)->finalize (object);
}

FuDevice *
fu_device_new (void)
{
	FuDevice *device;
	device = g_object_new (FU_TYPE_DEVICE, NULL);
	return FU_DEVICE (device);
}
