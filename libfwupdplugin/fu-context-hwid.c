/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuContext"

#include "config.h"

#include "fu-context-hwid.h"
#include "fu-context-private.h"
#include "fu-hwids.h"

/**
 * fu_context_get_hwid_keys:
 * @self: a #FuContext
 *
 * Returns all the defined HWID keys.
 *
 * Returns: (transfer container) (element-type utf8): All the known keys,
 * e.g. %FU_HWIDS_KEY_FAMILY
 *
 * Since: 1.8.10
 **/
GPtrArray *
fu_context_get_hwid_keys(FuContext *self)
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
	g_return_val_if_fail(FU_IS_CONTEXT(self), NULL);
	for (guint i = 0; keys[i] != NULL; i++)
		g_ptr_array_add(array, (gpointer)keys[i]);
	return array;
}

/**
 * fu_context_get_hwid_replace_keys:
 * @self: a #FuContext
 * @key: a HardwareID key, e.g. `HardwareID-3`
 *
 * Gets the replacement key for a well known value.
 *
 * Returns: the replacement value, e.g. `Manufacturer&ProductName`, or %NULL for error.
 *
 * Since: 1.8.10
 **/
const gchar *
fu_context_get_hwid_replace_keys(FuContext *self, const gchar *key)
{
	struct {
		const gchar *search;
		const gchar *replace;
	} msdefined[] = {
	    {"HardwareID-0",
	     FU_HWIDS_KEY_MANUFACTURER
	     "&" FU_HWIDS_KEY_FAMILY "&" FU_HWIDS_KEY_PRODUCT_NAME "&" FU_HWIDS_KEY_PRODUCT_SKU
	     "&" FU_HWIDS_KEY_BIOS_VENDOR "&" FU_HWIDS_KEY_BIOS_VERSION
	     "&" FU_HWIDS_KEY_BIOS_MAJOR_RELEASE "&" FU_HWIDS_KEY_BIOS_MINOR_RELEASE},
	    {"HardwareID-1",
	     FU_HWIDS_KEY_MANUFACTURER "&" FU_HWIDS_KEY_FAMILY "&" FU_HWIDS_KEY_PRODUCT_NAME
				       "&" FU_HWIDS_KEY_BIOS_VENDOR "&" FU_HWIDS_KEY_BIOS_VERSION
				       "&" FU_HWIDS_KEY_BIOS_MAJOR_RELEASE
				       "&" FU_HWIDS_KEY_BIOS_MINOR_RELEASE},
	    {"HardwareID-2",
	     FU_HWIDS_KEY_MANUFACTURER "&" FU_HWIDS_KEY_PRODUCT_NAME "&" FU_HWIDS_KEY_BIOS_VENDOR
				       "&" FU_HWIDS_KEY_BIOS_VERSION
				       "&" FU_HWIDS_KEY_BIOS_MAJOR_RELEASE
				       "&" FU_HWIDS_KEY_BIOS_MINOR_RELEASE},
	    {"HardwareID-3",
	     FU_HWIDS_KEY_MANUFACTURER
	     "&" FU_HWIDS_KEY_FAMILY "&" FU_HWIDS_KEY_PRODUCT_NAME "&" FU_HWIDS_KEY_PRODUCT_SKU
	     "&" FU_HWIDS_KEY_BASEBOARD_MANUFACTURER "&" FU_HWIDS_KEY_BASEBOARD_PRODUCT},
	    {"HardwareID-4",
	     FU_HWIDS_KEY_MANUFACTURER "&" FU_HWIDS_KEY_FAMILY "&" FU_HWIDS_KEY_PRODUCT_NAME
				       "&" FU_HWIDS_KEY_PRODUCT_SKU},
	    {"HardwareID-5",
	     FU_HWIDS_KEY_MANUFACTURER "&" FU_HWIDS_KEY_FAMILY "&" FU_HWIDS_KEY_PRODUCT_NAME},
	    {"HardwareID-6",
	     FU_HWIDS_KEY_MANUFACTURER "&" FU_HWIDS_KEY_PRODUCT_SKU
				       "&" FU_HWIDS_KEY_BASEBOARD_MANUFACTURER
				       "&" FU_HWIDS_KEY_BASEBOARD_PRODUCT},
	    {"HardwareID-7", FU_HWIDS_KEY_MANUFACTURER "&" FU_HWIDS_KEY_PRODUCT_SKU},
	    {"HardwareID-8",
	     FU_HWIDS_KEY_MANUFACTURER "&" FU_HWIDS_KEY_PRODUCT_NAME
				       "&" FU_HWIDS_KEY_BASEBOARD_MANUFACTURER
				       "&" FU_HWIDS_KEY_BASEBOARD_PRODUCT},
	    {"HardwareID-9", FU_HWIDS_KEY_MANUFACTURER "&" FU_HWIDS_KEY_PRODUCT_NAME},
	    {"HardwareID-10",
	     FU_HWIDS_KEY_MANUFACTURER "&" FU_HWIDS_KEY_FAMILY
				       "&" FU_HWIDS_KEY_BASEBOARD_MANUFACTURER
				       "&" FU_HWIDS_KEY_BASEBOARD_PRODUCT},
	    {"HardwareID-11", FU_HWIDS_KEY_MANUFACTURER "&" FU_HWIDS_KEY_FAMILY},
	    {"HardwareID-12", FU_HWIDS_KEY_MANUFACTURER "&" FU_HWIDS_KEY_ENCLOSURE_KIND},
	    {"HardwareID-13",
	     FU_HWIDS_KEY_MANUFACTURER "&" FU_HWIDS_KEY_BASEBOARD_MANUFACTURER
				       "&" FU_HWIDS_KEY_BASEBOARD_PRODUCT},
	    {"HardwareID-14", FU_HWIDS_KEY_MANUFACTURER},
	    {NULL, NULL}};

	g_return_val_if_fail(FU_IS_CONTEXT(self), NULL);
	g_return_val_if_fail(key != NULL, NULL);

	/* defined for Windows 10 */
	for (guint i = 0; msdefined[i].search != NULL; i++) {
		if (g_strcmp0(msdefined[i].search, key) == 0) {
			key = msdefined[i].replace;
			break;
		}
	}
	return key;
}

gboolean
fu_context_hwid_setup(FuContext *self, GError **error)
{
	/* add HardwareID GUIDs */
	for (guint i = 0; i < 15; i++) {
		g_autofree gchar *guid = NULL;
		g_autofree gchar *key = g_strdup_printf("HardwareID-%u", i);
		g_autoptr(GError) error_local = NULL;

		/* get the GUID and add to hash */
		guid = fu_context_get_hwid_guid(self, key, &error_local);
		if (guid == NULL) {
			g_debug("%s is not available, %s", key, error_local->message);
			continue;
		}
		fu_context_add_hwid_guid(self, guid);
	}

	/* success */
	return TRUE;
}

static gchar *
fu_context_hwid_get_guid_for_str(const gchar *str, GError **error)
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
		data[i] = GUINT16_TO_LE(data[i]);

	/* convert to a GUID */
	return fwupd_guid_hash_data((guint8 *)data,
				    items_written * 2,
				    FWUPD_GUID_FLAG_NAMESPACE_MICROSOFT);
}

/**
 * fu_context_get_hwid_guid:
 * @self: a #FuContext
 * @keys: a key, e.g. `HardwareID-3` or %FU_HWIDS_KEY_PRODUCT_SKU
 * @error: (nullable): optional return location for an error
 *
 * Gets the GUID for a specific key.
 *
 * Returns: a string, or %NULL for error.
 *
 * Since: 1.8.10
 **/
gchar *
fu_context_get_hwid_guid(FuContext *self, const gchar *keys, GError **error)
{
	g_autofree gchar *tmp = NULL;

	g_return_val_if_fail(FU_IS_CONTEXT(self), NULL);
	g_return_val_if_fail(keys != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	tmp = fu_context_get_hwid_replace_value(self, keys, error);
	if (tmp == NULL)
		return NULL;
	return fu_context_hwid_get_guid_for_str(tmp, error);
}
