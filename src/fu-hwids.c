/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include <glib.h>
#include <gio/gio.h>
#include <string.h>
#include <appstream-glib.h>

#include "fu-hwids.h"

struct _FuHwids {
	GObject			 parent_instance;
	GHashTable		*hash;
};

G_DEFINE_TYPE (FuHwids, fu_hwids, G_TYPE_OBJECT)

const gchar *
fu_hwids_get_value (FuHwids *self, const gchar *key)
{
	return g_hash_table_lookup (self->hash, key);
}

static gchar *
fu_hwids_get_guid_for_str (const gchar *str, GError **error)
{
#if AS_CHECK_VERSION(0,6,13)
	const gchar *namespace_id = "70ffd812-4c7f-4c7d-0000-000000000000";
	glong items_written = 0;
	g_autofree gunichar2 *data = NULL;

	/* convert to UTF-16 and convert to GUID using custom namespace */
	data = g_utf8_to_utf16 (str, -1, NULL, &items_written, error);
	if (data == NULL)
		return NULL;

	/* ensure the data is in little endian format */
	for (guint i = 0; i < items_written; i++)
		data[i] = GUINT16_TO_LE(data[i]);

	/* convert to a GUID */
	return as_utils_guid_from_data (namespace_id,
					(guint8*) data,
					items_written * 2,
					error);
#else
	g_set_error_literal (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_SUPPORTED,
			     "libappstream-glib 0.6.13 is required");
	return FALSE;
#endif
}

/**
 * fu_hwids_get_replace_keys:
 * @self: A #FuHwids
 * @key: A HardwareID key, e.g. "HardwareID-3"
 *
 * Gets the replacement key for a well known value.
 *
 * Returns: the replacement value, e.g. "Manufacturer&ProductName", or %NULL for error.
 **/
const gchar *
fu_hwids_get_replace_keys (FuHwids *self, const gchar *key)
{
	struct {
		const gchar *search;
		const gchar *replace;
	} msdefined[] = {
		{ "HardwareID-0",	FU_HWIDS_KEY_MANUFACTURER "&"
					FU_HWIDS_KEY_FAMILY "&"
					FU_HWIDS_KEY_PRODUCT_NAME "&"
					FU_HWIDS_KEY_PRODUCT_SKU "&"
					FU_HWIDS_KEY_BIOS_VENDOR "&"
					FU_HWIDS_KEY_BIOS_VERSION "&"
					FU_HWIDS_KEY_BIOS_MAJOR_RELEASE "&"
					FU_HWIDS_KEY_BIOS_MINOR_RELEASE },
		{ "HardwareID-1",	FU_HWIDS_KEY_MANUFACTURER "&"
					FU_HWIDS_KEY_FAMILY "&"
					FU_HWIDS_KEY_PRODUCT_NAME "&"
					FU_HWIDS_KEY_BIOS_VENDOR "&"
					FU_HWIDS_KEY_BIOS_VERSION "&"
					FU_HWIDS_KEY_BIOS_MAJOR_RELEASE "&"
					FU_HWIDS_KEY_BIOS_MINOR_RELEASE },
		{ "HardwareID-2",	FU_HWIDS_KEY_MANUFACTURER "&"
					FU_HWIDS_KEY_PRODUCT_NAME "&"
					FU_HWIDS_KEY_BIOS_VENDOR "&"
					FU_HWIDS_KEY_BIOS_VERSION "&"
					FU_HWIDS_KEY_BIOS_MAJOR_RELEASE "&"
					FU_HWIDS_KEY_BIOS_MINOR_RELEASE },
		{ "HardwareID-3",	FU_HWIDS_KEY_MANUFACTURER "&"
					FU_HWIDS_KEY_FAMILY "&"
					FU_HWIDS_KEY_PRODUCT_NAME "&"
					FU_HWIDS_KEY_PRODUCT_SKU "&"
					FU_HWIDS_KEY_BASEBOARD_MANUFACTURER "&"
					FU_HWIDS_KEY_BASEBOARD_PRODUCT },
		{ "HardwareID-4",	FU_HWIDS_KEY_MANUFACTURER "&"
					FU_HWIDS_KEY_FAMILY "&"
					FU_HWIDS_KEY_PRODUCT_NAME "&"
					FU_HWIDS_KEY_PRODUCT_SKU },
		{ "HardwareID-5",	FU_HWIDS_KEY_MANUFACTURER "&"
					FU_HWIDS_KEY_FAMILY "&"
					FU_HWIDS_KEY_PRODUCT_NAME },
		{ "HardwareID-6",	FU_HWIDS_KEY_MANUFACTURER "&"
					FU_HWIDS_KEY_PRODUCT_SKU "&"
					FU_HWIDS_KEY_BASEBOARD_MANUFACTURER "&"
					FU_HWIDS_KEY_BASEBOARD_PRODUCT },
		{ "HardwareID-7",	FU_HWIDS_KEY_MANUFACTURER "&"
					FU_HWIDS_KEY_PRODUCT_SKU },
		{ "HardwareID-8",	FU_HWIDS_KEY_MANUFACTURER "&"
					FU_HWIDS_KEY_PRODUCT_NAME "&"
					FU_HWIDS_KEY_BASEBOARD_MANUFACTURER "&"
					FU_HWIDS_KEY_BASEBOARD_PRODUCT },
		{ "HardwareID-9",	FU_HWIDS_KEY_MANUFACTURER "&"
					FU_HWIDS_KEY_PRODUCT_NAME },
		{ "HardwareID-10",	FU_HWIDS_KEY_MANUFACTURER "&"
					FU_HWIDS_KEY_FAMILY "&"
					FU_HWIDS_KEY_BASEBOARD_MANUFACTURER "&"
					FU_HWIDS_KEY_BASEBOARD_PRODUCT },
		{ "HardwareID-11",	FU_HWIDS_KEY_MANUFACTURER "&"
					FU_HWIDS_KEY_FAMILY },
		{ "HardwareID-12",	FU_HWIDS_KEY_MANUFACTURER "&"
					FU_HWIDS_KEY_ENCLOSURE_KIND },
		{ "HardwareID-13",	FU_HWIDS_KEY_MANUFACTURER "&"
					FU_HWIDS_KEY_BASEBOARD_MANUFACTURER "&"
					FU_HWIDS_KEY_BASEBOARD_PRODUCT },
		{ "HardwareID-14",	FU_HWIDS_KEY_MANUFACTURER },
		{ NULL, NULL }
	};

	/* defined for Windows 10 */
	for (guint i = 0; msdefined[i].search != NULL; i++) {
		if (g_strcmp0 (msdefined[i].search, key) == 0) {
			key = msdefined[i].replace;
			break;
		}
	}

	return key;
}

/**
 * fu_hwids_get_replace_values:
 * @self: A #FuHwids
 * @keys: A key, e.g. "HardwareID-3" or %FU_HWIDS_KEY_PRODUCT_SKU
 * @error: A #GError or %NULL
 *
 * Gets the replacement values for a HardwareID key or plain key.
 *
 * Returns: a string, e.g. "LENOVO&ThinkPad T440s", or %NULL for error.
 **/
gchar *
fu_hwids_get_replace_values (FuHwids *self, const gchar *keys, GError **error)
{
	g_auto(GStrv) split = NULL;
	g_autoptr(GString) str = g_string_new (NULL);

	/* do any replacements */
	keys = fu_hwids_get_replace_keys (self, keys);

	/* get each part of the HWID */
	split = g_strsplit (keys, "&", -1);
	for (guint j = 0; split[j] != NULL; j++) {
		const gchar *tmp = g_hash_table_lookup (self->hash, split[j]);
		if (tmp == NULL) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "not available as '%s' unknown",
				     split[j]);
			return NULL;
		}
		g_string_append_printf (str, "%s&", tmp);
	}
	if (str->len > 0)
		g_string_truncate (str, str->len - 1);
	return g_strdup (str->str);
}

/**
 * fu_hwids_get_guid:
 * @self: A #FuHwids
 * @keys: A key, e.g. "HardwareID-3" or %FU_HWIDS_KEY_PRODUCT_SKU
 * @error: A #GError or %NULL
 *
 * Gets the GUID for a specific key.
 *
 * Returns: a string, or %NULL for error.
 **/
gchar *
fu_hwids_get_guid (FuHwids *self, const gchar *keys, GError **error)
{
	g_autofree gchar *tmp = fu_hwids_get_replace_values (self, keys, error);
	if (tmp == NULL)
		return NULL;
	return fu_hwids_get_guid_for_str (tmp, error);
}

/**
 * fu_hwids_setup:
 * @self: A #FuHwids
 * @sysfsdir: The sysfs directory, or %NULL for the default
 * @error: A #GError or %NULL
 *
 * Reads all the SMBIOS values from the hardware.
 *
 * Returns: %TRUE for success
 **/
gboolean
fu_hwids_setup (FuHwids *self, const gchar *sysfsdir, GError **error)
{
	struct {
		const gchar *key;
		const gchar *value;
	} sysfsfile[] = {
		{ FU_HWIDS_KEY_MANUFACTURER,		"sys_vendor" },
		{ FU_HWIDS_KEY_ENCLOSURE_KIND,		"chassis_type" },
		{ FU_HWIDS_KEY_FAMILY,			"product_family" },
		{ FU_HWIDS_KEY_PRODUCT_NAME,		"product_name" },
		{ FU_HWIDS_KEY_PRODUCT_SKU,		"product_sku" },
		{ FU_HWIDS_KEY_BIOS_VENDOR,		"bios_vendor" },
		{ FU_HWIDS_KEY_BIOS_VERSION,		"bios_version" },
		{ FU_HWIDS_KEY_BIOS_MAJOR_RELEASE,	"bios_major_release" },
		{ FU_HWIDS_KEY_BIOS_MINOR_RELEASE,	"bios_minor_release" },
		{ FU_HWIDS_KEY_BASEBOARD_MANUFACTURER,	"board_vendor" },
		{ FU_HWIDS_KEY_BASEBOARD_PRODUCT,	"board_name" },
		{ NULL, NULL }
	};

	g_return_val_if_fail (FU_IS_HWIDS (self), FALSE);

	/* default value */
	if (sysfsdir == NULL)
		sysfsdir = "/sys/class/dmi/id";

	/* does not exist in a container */
	if (!g_file_test (sysfsdir, G_FILE_TEST_EXISTS))
		return TRUE;

	/* get all DMI data */
	for (guint i = 0; sysfsfile[i].key != NULL; i++) {
		g_autofree gchar *contents = NULL;
		g_autofree gchar *fn = NULL;
		const gchar *contents_hdr;

		fn = g_build_filename (sysfsdir, sysfsfile[i].value, NULL);
		if (!g_file_test (fn, G_FILE_TEST_EXISTS)) {
			g_debug ("no %s so ignoring", fn);
			continue;
		}
		if (!g_file_get_contents (fn, &contents, NULL, error))
			return FALSE;
		g_strdelimit (contents, "\n\r", '\0');
		g_debug ("smbios property %s=%s", fn, contents);
		if (g_strcmp0 (contents, "Not Available") == 0)
			continue;
		if (g_strcmp0 (contents, "Not Defined") == 0)
			continue;

		/* weirdly, remove leading zeros */
		contents_hdr = contents;
		while (contents_hdr[0] == '0')
			contents_hdr++;
		g_hash_table_insert (self->hash,
				     g_strdup (sysfsfile[i].key),
				     g_strdup (contents_hdr));
	}

	return TRUE;
}

static void
fu_hwids_finalize (GObject *object)
{
	FuHwids *self;
	g_return_if_fail (FU_IS_HWIDS (object));
	self = FU_HWIDS (object);

	g_hash_table_unref (self->hash);
	G_OBJECT_CLASS (fu_hwids_parent_class)->finalize (object);
}

static void
fu_hwids_class_init (FuHwidsClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = fu_hwids_finalize;
}

static void
fu_hwids_init (FuHwids *self)
{
	self->hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
}

FuHwids *
fu_hwids_new (void)
{
	FuHwids *self;
	self = g_object_new (FU_TYPE_HWIDS, NULL);
	return FU_HWIDS (self);
}
