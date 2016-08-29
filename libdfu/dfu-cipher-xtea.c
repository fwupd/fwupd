/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
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

#include "config.h"

#include <string.h>

#include "dfu-cipher-xtea.h"
#include "dfu-error.h"

#define XTEA_DELTA		0x9e3779b9
#define XTEA_NUM_ROUNDS		32

static gboolean
dfu_tool_parse_xtea_key (const gchar *key, guint32 *keys, GError **error)
{
	guint i;
	gsize key_len;
	g_autofree gchar *key_pad = NULL;

	/* too long */
	key_len = strlen (key);
	if (key_len > 32) {
		g_set_error (error,
			     DFU_ERROR,
			     DFU_ERROR_NOT_SUPPORTED,
			     "Key string too long at %" G_GSIZE_FORMAT " chars, max 16",
			     key_len);
		return FALSE;
	}

	/* parse 4x32b values or generate a hash */
	if (key_len == 32) {
		for (i = 0; i < 4; i++) {
			gchar buf[] = "xxxxxxxx";
			gchar *endptr;
			guint64 tmp;

			/* copy to 4-char buf (with NUL) */
			memcpy (buf, key + i*8, 8);
			tmp = g_ascii_strtoull (buf, &endptr, 16);
			if (endptr && endptr[0] != '\0') {
				g_set_error (error,
					     DFU_ERROR,
					     DFU_ERROR_NOT_SUPPORTED,
					     "Failed to parse key '%s'", key);
				return FALSE;
			}
			keys[3-i] = (guint32) tmp;
		}
	} else {
		gsize buf_len = 16;
		g_autoptr(GChecksum) csum = NULL;
		csum = g_checksum_new (G_CHECKSUM_MD5);
		g_checksum_update (csum, (const guchar *) key, (gssize) key_len);
		g_checksum_get_digest (csum, (guint8 *) keys, &buf_len);
		g_assert (buf_len == 16);
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
	guint32 *tmp = (guint32 *) data;
	guint32 v0;
	guint32 v1;
	guint8 i;
	guint j;
	guint32 keys[4];

	if (!dfu_tool_parse_xtea_key (key, keys, error))
		return FALSE;
	for (j = 0; j < length / 4; j += 2) {
		v0 = tmp[j];
		v1 = tmp[j+1];
		sum = XTEA_DELTA * XTEA_NUM_ROUNDS;
		for (i = 0; i < XTEA_NUM_ROUNDS; i++) {
			v1 -= (((v0 << 4) ^ (v0 >> 5)) + v0) ^ (sum + keys[(sum >> 11) & 3]);
			sum -= XTEA_DELTA;
			v0 -= (((v1 << 4) ^ (v1 >> 5)) + v1) ^ (sum + keys[sum & 3]);
		}
		tmp[j] = v0;
		tmp[j+1] = v1;
	}
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
	guint32 *tmp = (guint32 *) data;
	guint32 v0;
	guint32 v1;
	guint8 i;
	guint j;
	guint32 keys[4];

	if (!dfu_tool_parse_xtea_key (key, keys, error))
		return FALSE;
	for (j = 0; j < length / 4; j += 2) {
		sum = 0;
		v0 = tmp[j];
		v1 = tmp[j+1];
		for (i = 0; i < XTEA_NUM_ROUNDS; i++) {
			v0 += (((v1 << 4) ^ (v1 >> 5)) + v1) ^ (sum + keys[sum & 3]);
			sum += XTEA_DELTA;
			v1 += (((v0 << 4) ^ (v0 >> 5)) + v0) ^ (sum + keys[(sum >> 11) & 3]);
		}
		tmp[j] = v0;
		tmp[j+1] = v1;
	}
	return TRUE;
}
