/*
 * Copyright (C) 2015-2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fu-common.h"

#include "dfu-element.h"
#include "dfu-format-metadata.h"
#include "dfu-image.h"

#include "fwupd-error.h"

/**
 * dfu_firmware_from_metadata: (skip)
 * @firmware: a #DfuFirmware
 * @bytes: data to parse
 * @flags: some #DfuFirmwareParseFlags
 * @error: a #GError, or %NULL
 *
 * Unpacks into a firmware object from metadata data.
 *
 * The representation in memory is as follows:
 *
 * uint16      signature='MD'
 * uint8       number_of_keys
 * uint8       number_of_keys
 * uint8       key(n)_length
 * ...         key(n) (no NUL)
 * uint8       value(n)_length
 * ...         value(n) (no NUL)
 * <existing DFU footer>

 * Returns: %TRUE for success
 **/
gboolean
dfu_firmware_from_metadata (DfuFirmware *firmware,
			    GBytes *bytes,
			    DfuFirmwareParseFlags flags,
			    GError **error)
{
	const guint8 *data;
	gsize data_length;
	guint idx = 2;
	guint kvlen;
	guint number_keys;

	/* not big enough */
	data = g_bytes_get_data (bytes, &data_length);
	if (data_length <= 0x10)
		return TRUE;

	/* signature invalid */
	if (memcmp (data, "MD", 2) != 0)
		return TRUE;

	/* parse key=value store */
	number_keys = data[idx++];
	for (guint i = 0; i < number_keys; i++) {
		g_autofree gchar *key = NULL;
		g_autofree gchar *value = NULL;

		/* parse key */
		kvlen = data[idx++];
		if (kvlen > 233) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "metadata table corrupt, key=%u",
				     kvlen);
			return FALSE;
		}
		if (idx + kvlen + 0x10 > data_length) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "metadata table corrupt, k-kvlen=%u",
				     kvlen);
			return FALSE;
		}
		key = g_strndup ((const gchar *) data + idx, kvlen);
		idx += kvlen;

		/* parse value */
		kvlen = data[idx++];
		if (kvlen > 233) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "metadata table corrupt, value=%u",
				     kvlen);
			return FALSE;
		}
		if (idx + kvlen + 0x10 > data_length) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "metadata table corrupt, v-kvlen=%u",
				     kvlen);
			return FALSE;
		}
		value = g_strndup ((const gchar *) data + idx, kvlen);
		idx += kvlen;
		dfu_firmware_set_metadata (firmware, key, value);
	}
	return TRUE;
}

/**
 * dfu_firmware_to_metadata: (skip)
 * @firmware: a #DfuFirmware
 * @error: a #GError, or %NULL
 *
 * Packs metadata firmware
 *
 * Returns: (transfer full): the packed data
 **/
GBytes *
dfu_firmware_to_metadata (DfuFirmware *firmware, GError **error)
{
	GHashTable *metadata;
	guint8 mdbuf[239];
	guint idx = 0;
	guint number_keys;
	g_autoptr(GList) keys = NULL;

	/* no metadata */
	metadata = dfu_firmware_get_metadata_table (firmware);
	if (g_hash_table_size (metadata) == 0)
		return g_bytes_new (NULL, 0);

	/* check the number of keys */
	keys = g_hash_table_get_keys (metadata);
	number_keys = g_list_length (keys);
	if (number_keys > 59) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "too many metadata keys (%u)",
			     number_keys);
		return NULL;
	}

	/* write the signature */
	mdbuf[idx++] = 'M';
	mdbuf[idx++] = 'D';
	mdbuf[idx++] = (guint8) number_keys;
	for (GList *l = keys; l != NULL; l = l->next) {
		const gchar *key;
		const gchar *value;
		guint key_len;
		guint value_len;

		/* check key and value length */
		key = l->data;
		key_len = (guint) strlen (key);
		if (key_len > 233) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "metadata key too long: %s",
				     key);
			return NULL;
		}
		value = g_hash_table_lookup (metadata, key);
		value_len = (guint) strlen (value);
		if (value_len > 233) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "value too long: %s",
				     value);
			return NULL;
		}

		/* do we still have space? */
		if (idx + key_len + value_len + 2 > sizeof(mdbuf)) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "not enough space in metadata table, "
				     "already used %u bytes", idx);
			return NULL;
		}

		/* write the key */
		mdbuf[idx++] = (guint8) key_len;
		if (!fu_memcpy_safe (mdbuf, sizeof(mdbuf), idx,			/* dst */
				     (const guint8 *) key, key_len, 0x0,	/* src */
				     key_len, error))
			return NULL;
		idx += key_len;

		/* write the value */
		mdbuf[idx++] = (guint8) value_len;
		if (!fu_memcpy_safe (mdbuf, sizeof(mdbuf), idx,			/* dst */
				     (const guint8 *) value, value_len, 0x0,	/* src */
				     value_len, error))
			return NULL;
		idx += value_len;
	}
	g_debug ("metadata table was %u/%" G_GSIZE_FORMAT " bytes",
		 idx, sizeof(mdbuf));
	return g_bytes_new (mdbuf, idx);
}
