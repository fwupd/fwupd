/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fu-common.h"

#include "dfu-cipher-xtea.h"

#include "fwupd-error.h"

#define XTEA_DELTA		0x9e3779b9
#define XTEA_NUM_ROUNDS		32

static void
dfu_cipher_buf_to_uint32 (const guint8 *buf, guint buflen, guint32 *array)
{
	for (guint i = 0; i < buflen / 4; i++)
		array[i] = fu_common_read_uint32 (&buf[i * 4], G_LITTLE_ENDIAN);
}

static void
dfu_cipher_uint32_to_buf (guint8 *buf, guint buflen, const guint32 *array)
{
	for (guint i = 0; i < buflen / 4; i++)
		fu_common_write_uint32 (&buf[i * 4], array[i], G_LITTLE_ENDIAN);
}

static gboolean
dfu_tool_parse_xtea_key (const gchar *key, guint32 *keys, GError **error)
{
	gsize key_len;

	/* too long */
	key_len = strlen (key);
	if (key_len > 32) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "Key string too long at %" G_GSIZE_FORMAT " chars, max 16",
			     key_len);
		return FALSE;
	}

	/* parse 4x32b values or generate a hash */
	if (key_len == 32) {
		for (guint8 i = 0; i < 4; i++) {
			gchar buf[] = "xxxxxxxx";
			gchar *endptr;
			guint64 tmp;

			/* copy to 4-char buf (with NUL) */
			memcpy (buf, key + i*8, 8);
			tmp = g_ascii_strtoull (buf, &endptr, 16);
			if (endptr && endptr[0] != '\0') {
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_NOT_SUPPORTED,
					     "Failed to parse key '%s'", key);
				return FALSE;
			}
			keys[3-i] = (guint32) tmp;
		}
	} else {
		gsize buf_len = 16;
		guint8 buf[16];
		g_autoptr(GChecksum) csum = NULL;
		csum = g_checksum_new (G_CHECKSUM_MD5);
		g_checksum_update (csum, (const guchar *) key, (gssize) key_len);
		g_checksum_get_digest (csum, buf, &buf_len);
		g_assert (buf_len == 16);
		dfu_cipher_buf_to_uint32 (buf, buf_len, keys);
	}

	/* success */
	g_debug ("using XTEA key %04x%04x%04x%04x",
		 keys[3], keys[2], keys[1], keys[0]);
	return TRUE;
}

/**
 * dfu_cipher_decrypt_xtea: (skip)
 * @key: a XTEA key
 * @data: data to parse
 * @length: length of @data
 * @error: a #GError, or %NULL
 *
 * Decrypt a buffer using XTEA.
 *
 * Returns: %TRUE for success
 **/
gboolean
dfu_cipher_decrypt_xtea (const gchar *key,
			 guint8 *data,
			 guint32 length,
			 GError **error)
{
	guint32 sum;
	guint32 v0;
	guint32 v1;
	guint32 chunks = length / 4;
	guint32 keys[4];
	g_autofree guint32 *tmp = NULL;

	/* sanity check */
	if (length < 8) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "8 bytes data required, got %" G_GUINT32_FORMAT,
			     length);
		return FALSE;
	}
	if (length % 4 != 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "Multiples of 4 bytes required, got %" G_GUINT32_FORMAT,
			     length);
		return FALSE;
	}

	/* parse key */
	if (!dfu_tool_parse_xtea_key (key, keys, error))
		return FALSE;

	/* allocate a buffer that can be addressed in 4-byte chunks */
	tmp = g_new0 (guint32, chunks);
	dfu_cipher_buf_to_uint32 (data, length, tmp);

	/* process buffer using XTEA keys */
	for (guint j = 0; j < chunks; j += 2) {
		v0 = tmp[j];
		v1 = tmp[j+1];
		sum = XTEA_DELTA * XTEA_NUM_ROUNDS;
		for (guint8 i = 0; i < XTEA_NUM_ROUNDS; i++) {
			v1 -= (((v0 << 4) ^ (v0 >> 5)) + v0) ^ (sum + keys[(sum >> 11) & 3]);
			sum -= XTEA_DELTA;
			v0 -= (((v1 << 4) ^ (v1 >> 5)) + v1) ^ (sum + keys[sum & 3]);
		}
		tmp[j] = v0;
		tmp[j+1] = v1;
	}

	/* copy the temp buffer back to data */
	dfu_cipher_uint32_to_buf (data, length, tmp);
	return TRUE;
}

/**
 * dfu_cipher_encrypt_xtea: (skip)
 * @key: a XTEA key
 * @data: data to parse
 * @length: length of @data
 * @error: a #GError, or %NULL
 *
 * Encrypt a buffer using XTEA.
 *
 * Returns: %TRUE for success
 **/
gboolean
dfu_cipher_encrypt_xtea (const gchar *key,
			 guint8 *data,
			 guint32 length,
			 GError **error)
{
	guint32 sum;
	guint32 v0;
	guint32 v1;
	guint32 chunks = length / 4;
	guint32 keys[4];
	g_autofree guint32 *tmp = NULL;

	/* sanity check */
	if (length < 8) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "8 bytes data required, got %" G_GUINT32_FORMAT,
			     length);
		return FALSE;
	}
	if (length % 4 != 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "Multiples of 4 bytes required, got %" G_GUINT32_FORMAT,
			     length);
		return FALSE;
	}

	/* parse key */
	if (!dfu_tool_parse_xtea_key (key, keys, error))
		return FALSE;

	/* allocate a buffer that can be addressed in 4-byte chunks */
	tmp = g_new0 (guint32, chunks);
	dfu_cipher_buf_to_uint32 (data, length, tmp);

	/* process buffer using XTEA keys */
	for (guint j = 0; j < chunks; j += 2) {
		sum = 0;
		v0 = tmp[j];
		v1 = tmp[j+1];
		for (guint8 i = 0; i < XTEA_NUM_ROUNDS; i++) {
			v0 += (((v1 << 4) ^ (v1 >> 5)) + v1) ^ (sum + keys[sum & 3]);
			sum += XTEA_DELTA;
			v1 += (((v0 << 4) ^ (v0 >> 5)) + v0) ^ (sum + keys[(sum >> 11) & 3]);
		}
		tmp[j] = v0;
		tmp[j+1] = v1;
	}

	/* copy the temp buffer back to data */
	dfu_cipher_uint32_to_buf (data, length, tmp);
	return TRUE;
}
