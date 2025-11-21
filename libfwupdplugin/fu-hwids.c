/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuHwids"

#include "config.h"

#include <gio/gio.h>
#include <string.h>

#include "fwupd-common.h"
#include "fwupd-error.h"

#include "fu-common.h"
#include "fu-hwids-private.h"
#include "fu-path.h"
#include "fu-string.h"

/**
 * FuHwids:
 *
 * A the hardware IDs on the system.
 *
 * Note, these are called "CHIDs" in Microsoft Windows and the results here
 * will match that of `ComputerHardwareIds.exe`.
 *
 * See also: [class@FuSmbios]
 */

struct _FuHwids {
	GObject parent_instance;
	GHashTable *hash_values;	 /* BiosVersion->"1.2.3 " */
	GHashTable *hash_values_display; /* BiosVersion->"1.2.3" */
	GHashTable *hash_guid;		 /* a-c-b-d->1 */
	GPtrArray *array_guids;		 /* a-c-b-d */
	GHashTable *chids;		 /* "HardwareID-5"->"Manufacturer&ProductName" */
};

G_DEFINE_TYPE(FuHwids, fu_hwids, G_TYPE_OBJECT)

/**
 * fu_hwids_get_value:
 * @self: a #FuHwids
 * @key: a DMI ID, e.g. `BiosVersion`
 *
 * Gets the cached value for one specific key that is valid ASCII and suitable
 * for display.
 *
 * Returns: the string, e.g. `1.2.3`, or %NULL if not found
 *
 * Since: 0.9.3
 **/
const gchar *
fu_hwids_get_value(FuHwids *self, const gchar *key)
{
	return g_hash_table_lookup(self->hash_values_display, key);
}

/**
 * fu_hwids_has_guid:
 * @self: a #FuHwids
 * @guid: a GUID, e.g. `059eb22d-6dc7-59af-abd3-94bbe017f67c`
 *
 * Finds out if a hardware GUID exists.
 *
 * Returns: %TRUE if the GUID exists
 *
 * Since: 0.9.3
 **/
gboolean
fu_hwids_has_guid(FuHwids *self, const gchar *guid)
{
	return g_hash_table_lookup(self->hash_guid, guid) != NULL;
}

/**
 * fu_hwids_get_guids:
 * @self: a #FuHwids
 *
 * Returns all the defined HWIDs
 *
 * Returns: (transfer none) (element-type utf8): an array of GUIDs
 *
 * Since: 0.9.3
 **/
GPtrArray *
fu_hwids_get_guids(FuHwids *self)
{
	return self->array_guids;
}

/**
 * fu_hwids_get_keys:
 * @self: a #FuHwids
 *
 * Returns all the defined HWID keys.
 *
 * Returns: (transfer container) (element-type utf8): All the known keys,
 * e.g. %FU_HWIDS_KEY_FAMILY
 *
 * Since: 1.5.6
 **/
GPtrArray *
fu_hwids_get_keys(FuHwids *self)
{
	GPtrArray *array = g_ptr_array_new();
	const gchar *keys[] = {FU_HWIDS_KEY_BIOS_VENDOR,
			       FU_HWIDS_KEY_BIOS_VERSION,
			       FU_HWIDS_KEY_BIOS_MAJOR_RELEASE,
			       FU_HWIDS_KEY_BIOS_MINOR_RELEASE,
			       FU_HWIDS_KEY_FIRMWARE_MAJOR_RELEASE,
			       FU_HWIDS_KEY_FIRMWARE_MINOR_RELEASE,
			       FU_HWIDS_KEY_MANUFACTURER,
			       FU_HWIDS_KEY_FAMILY,
			       FU_HWIDS_KEY_PRODUCT_NAME,
			       FU_HWIDS_KEY_PRODUCT_SKU,
			       FU_HWIDS_KEY_ENCLOSURE_KIND,
			       FU_HWIDS_KEY_BASEBOARD_MANUFACTURER,
			       FU_HWIDS_KEY_BASEBOARD_PRODUCT,
			       NULL};
	g_return_val_if_fail(FU_IS_HWIDS(self), NULL);
	for (guint i = 0; keys[i] != NULL; i++)
		g_ptr_array_add(array, (gpointer)keys[i]);
	return array;
}

static gchar *
fu_hwids_get_guid_for_str(const gchar *str, GError **error)
{
	glong items_written = 0;
	g_autofree gunichar2 *data = NULL;

	/* convert to UTF-16 and convert to GUID using custom namespace */
	data = g_utf8_to_utf16(str, -1, NULL, &items_written, error);
	if (data == NULL)
		return NULL;

	if (items_written == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "no GUIDs in data");
		return NULL;
	}

	/* ensure the data is in little endian format */
	for (glong i = 0; i < items_written; i++)
		data[i] = GUINT16_TO_LE(data[i]); /* nocheck:blocked */

	/* convert to a GUID */
	return fwupd_guid_hash_data((guint8 *)data,
				    items_written * 2,
				    FWUPD_GUID_FLAG_NAMESPACE_MICROSOFT);
}

/**
 * fu_hwids_get_replace_keys:
 * @self: a #FuHwids
 * @key: a CHID key, e.g. `HardwareID-03`
 *
 * Gets the defined values for a well known value.
 *
 * Returns: the replacement value, e.g. `Manufacturer&ProductName`, or %NULL for error.
 *
 * Since: 0.9.3
 **/
const gchar *
fu_hwids_get_replace_keys(FuHwids *self, const gchar *key)
{
	const gchar *value;

	g_return_val_if_fail(FU_IS_HWIDS(self), NULL);
	g_return_val_if_fail(key != NULL, NULL);

	value = g_hash_table_lookup(self->chids, key);
	if (value != NULL)
		return value;
	return key;
}

/**
 * fu_hwids_add_chid:
 * @self: a #FuHwids
 * @key: an textual ID, e.g. `HardwareID-05`
 * @value: a composite hardware key, e.g. `Manufacturer&ProductName`
 *
 * Defines a "Computer Hardware ID" in terms of a set of SMBIOS values.
 *
 * Since: 1.9.16
 **/
void
fu_hwids_add_chid(FuHwids *self, const gchar *key, const gchar *value)
{
	g_return_if_fail(FU_IS_HWIDS(self));
	g_return_if_fail(key != NULL);
	g_return_if_fail(value != NULL);
	g_hash_table_insert(self->chids, g_strdup(key), g_strdup(value));
}

static gint
fu_hwids_sort_keys_cb(gconstpointer a, gconstpointer b)
{
	const gchar *key1 = *((gchar **)a);
	const gchar *key2 = *((gchar **)b);
	return g_strcmp0(key1, key2);
}

/**
 * fu_hwids_get_chid_keys:
 * @self: a #FuHwids
 *
 * Returns all the CHID keys added by fu_hwids_add_chid().
 *
 * Returns: (transfer container) (element-type utf8): IDs
 *
 * Since: 1.9.16
 **/
GPtrArray *
fu_hwids_get_chid_keys(FuHwids *self)
{
	GHashTableIter iter;
	gpointer key;
	g_autoptr(GPtrArray) keys = g_ptr_array_new_with_free_func(g_free);

	g_return_val_if_fail(FU_IS_HWIDS(self), NULL);

	g_hash_table_iter_init(&iter, self->chids);
	while (g_hash_table_iter_next(&iter, &key, NULL))
		g_ptr_array_add(keys, g_strdup(key));
	g_ptr_array_sort(keys, fu_hwids_sort_keys_cb);

	return g_steal_pointer(&keys);
}

/**
 * fu_hwids_add_value:
 * @self: a #FuHwids
 * @key: a key, e.g. %FU_HWIDS_KEY_PRODUCT_SKU
 * @value: (nullable): a new value, e.g. `ExampleModel`
 *
 * Sets override values so you can emulate another system.
 *
 * This function has no effect if called after fu_hwids_setup()
 *
 * Since: 1.8.10
 **/
void
fu_hwids_add_value(FuHwids *self, const gchar *key, const gchar *value)
{
	g_return_if_fail(FU_IS_HWIDS(self));
	g_return_if_fail(key != NULL);

	/* does not replace; first value set wins */
	if (g_hash_table_contains(self->hash_values, key))
		return;
	g_hash_table_insert(self->hash_values, g_strdup(key), g_strdup(value));

	/* make suitable for display */
	if (value != NULL) {
		g_autofree gchar *value_safe = g_str_to_ascii(value, "C");
		g_strdelimit(value_safe, "\n\r", '\0');
		g_strchomp(value_safe);
		g_hash_table_insert(self->hash_values_display,
				    g_strdup(key),
				    g_steal_pointer(&value_safe));
	} else {
		g_hash_table_insert(self->hash_values_display, g_strdup(key), NULL);
	}
}

/**
 * fu_hwids_get_replace_values:
 * @self: a #FuHwids
 * @keys: a key, e.g. `HardwareID-3` or %FU_HWIDS_KEY_PRODUCT_SKU
 * @error: (nullable): optional return location for an error
 *
 * Gets the replacement values for a HardwareID key or plain key.
 *
 * Returns: a string, e.g. `LENOVO&ThinkPad T440s`, or %NULL for error.
 *
 * Since: 0.9.3
 **/
gchar *
fu_hwids_get_replace_values(FuHwids *self, const gchar *keys, GError **error)
{
	g_auto(GStrv) split = NULL;
	g_autoptr(GString) str = g_string_new(NULL);

	g_return_val_if_fail(FU_IS_HWIDS(self), NULL);
	g_return_val_if_fail(keys != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* do any replacements */
	keys = fu_hwids_get_replace_keys(self, keys);

	/* get each part of the HWID */
	split = g_strsplit(keys, "&", -1);
	for (guint j = 0; split[j] != NULL; j++) {
		const gchar *tmp = g_hash_table_lookup(self->hash_values, split[j]);
		if (tmp == NULL) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "not available as '%s' unknown",
				    split[j]);
			return NULL;
		}
		g_string_append_printf(str, "%s&", tmp);
	}
	if (str->len > 0)
		g_string_truncate(str, str->len - 1);
	return g_strdup(str->str);
}

/**
 * fu_hwids_get_guid:
 * @self: a #FuHwids
 * @keys: a key, e.g. `HardwareID-3` or %FU_HWIDS_KEY_PRODUCT_SKU
 * @error: (nullable): optional return location for an error
 *
 * Gets the GUID for a specific key.
 *
 * Returns: a string, or %NULL for error.
 *
 * Since: 0.9.3
 **/
gchar *
fu_hwids_get_guid(FuHwids *self, const gchar *keys, GError **error)
{
	g_autofree gchar *tmp = NULL;

	g_return_val_if_fail(FU_IS_HWIDS(self), NULL);
	g_return_val_if_fail(keys != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	tmp = fu_hwids_get_replace_values(self, keys, error);
	if (tmp == NULL)
		return NULL;
	return fu_hwids_get_guid_for_str(tmp, error);
}

/**
 * fu_hwids_add_guid:
 * @self: a #FuHwids
 * @guid: a GUID
 *
 * Adds a HWID GUID value.
 *
 * Since: 1.8.10
 **/
void
fu_hwids_add_guid(FuHwids *self, const gchar *guid)
{
	g_return_if_fail(FU_IS_HWIDS(self));
	g_return_if_fail(guid != NULL);
	g_hash_table_insert(self->hash_guid, g_strdup(guid), GUINT_TO_POINTER(1));
	g_ptr_array_add(self->array_guids, g_strdup(guid));
}

/**
 * fu_hwids_setup:
 * @self: a #FuHwids
 * @error: (nullable): optional return location for an error
 *
 * Adds all the `HardwareID` GUIDs from the previously supplied data.
 *
 * Returns: %TRUE for success
 *
 * Since: 0.9.3
 **/
gboolean
fu_hwids_setup(FuHwids *self, GError **error)
{
	g_autoptr(GPtrArray) chids = fu_hwids_get_chid_keys(self);

	g_return_val_if_fail(FU_IS_HWIDS(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* add GUIDs */
	for (guint i = 0; i < chids->len; i++) {
		const gchar *key = g_ptr_array_index(chids, i);
		g_autofree gchar *guid = NULL;
		g_autoptr(GError) error_local = NULL;

		/* get the GUID and add to hash */
		guid = fu_hwids_get_guid(self, key, &error_local);
		if (guid == NULL) {
			g_debug("%s is not available, %s", key, error_local->message);
			continue;
		}
		fu_hwids_add_guid(self, guid);
	}

	return TRUE;
}

static void
fu_hwids_finalize(GObject *object)
{
	FuHwids *self;
	g_return_if_fail(FU_IS_HWIDS(object));
	self = FU_HWIDS(object);

	g_hash_table_unref(self->hash_values);
	g_hash_table_unref(self->hash_values_display);
	g_hash_table_unref(self->hash_guid);
	g_hash_table_unref(self->chids);
	g_ptr_array_unref(self->array_guids);

	G_OBJECT_CLASS(fu_hwids_parent_class)->finalize(object);
}

static void
fu_hwids_class_init(FuHwidsClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_hwids_finalize;
}

static void
fu_hwids_init(FuHwids *self)
{
	self->hash_values = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	self->hash_values_display = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	self->hash_guid = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	self->chids = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	self->array_guids = g_ptr_array_new_with_free_func(g_free);

	/* Windows 10 CHIDs */
	fu_hwids_add_chid(self,
			  "HardwareID-00",
			  "Manufacturer&Family&ProductName&ProductSku&BiosVendor&BiosVersion&"
			  "BiosMajorRelease&BiosMinorRelease");
	fu_hwids_add_chid(self,
			  "HardwareID-01",
			  "Manufacturer&Family&ProductName&BiosVendor&BiosVersion&"
			  "BiosMajorRelease&BiosMinorRelease");
	fu_hwids_add_chid(self,
			  "HardwareID-02",
			  "Manufacturer&ProductName&BiosVendor&BiosVersion&"
			  "BiosMajorRelease&BiosMinorRelease");
	fu_hwids_add_chid(self,
			  "HardwareID-03",
			  "Manufacturer&Family&ProductName&ProductSku&"
			  "BaseboardManufacturer&BaseboardProduct");
	fu_hwids_add_chid(self, "HardwareID-04", "Manufacturer&Family&ProductName&ProductSku");
	fu_hwids_add_chid(self, "HardwareID-05", "Manufacturer&Family&ProductName");
	fu_hwids_add_chid(self,
			  "HardwareID-06",
			  "Manufacturer&ProductSku&BaseboardManufacturer&BaseboardProduct");
	fu_hwids_add_chid(self, "HardwareID-07", "Manufacturer&ProductSku");
	fu_hwids_add_chid(self,
			  "HardwareID-08",
			  "Manufacturer&ProductName&BaseboardManufacturer&BaseboardProduct");
	fu_hwids_add_chid(self, "HardwareID-09", "Manufacturer&ProductName");
	fu_hwids_add_chid(self,
			  "HardwareID-10",
			  "Manufacturer&Family&BaseboardManufacturer&BaseboardProduct");
	fu_hwids_add_chid(self, "HardwareID-11", "Manufacturer&Family");
	fu_hwids_add_chid(self, "HardwareID-12", "Manufacturer&EnclosureKind");
	fu_hwids_add_chid(self,
			  "HardwareID-13",
			  "Manufacturer&BaseboardManufacturer&BaseboardProduct");
	fu_hwids_add_chid(self, "HardwareID-14", "Manufacturer");

	/* used by the flashrom plugin */
	fu_hwids_add_chid(self,
			  "fwupd-04",
			  "Manufacturer&Family&ProductName&ProductSku&BiosVendor");
	fu_hwids_add_chid(self, "fwupd-05", "Manufacturer&Family&ProductName&BiosVendor");
	fu_hwids_add_chid(self, "fwupd-14", "Manufacturer&BiosVendor");
}

/**
 * fu_hwids_new:
 *
 * Creates a new #FuHwids
 *
 * Since: 0.9.3
 **/
FuHwids *
fu_hwids_new(void)
{
	FuHwids *self;
	self = g_object_new(FU_TYPE_HWIDS, NULL);
	return FU_HWIDS(self);
}
