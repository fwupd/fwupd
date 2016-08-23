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

#include "dfu-cipher-devo.h"
#include "dfu-error.h"

/* this is not really a cipher, more just obfuscation and is specific to the
 * Walkera Devo line of RC controllers */

static gboolean
dfu_tool_parse_devo_key (const gchar *key, guint8 *offset, GError **error)
{
	gchar *endptr;
	guint64 tmp;

	tmp = g_ascii_strtoull (key, &endptr, 10);
	if (tmp > 0xff || endptr[0] != '\0') {
		g_set_error (error,
			     DFU_ERROR,
			     DFU_ERROR_INTERNAL,
			     "Failed to parse offset value '%s'", key);
		return FALSE;
	}

	/* success */
	if (offset != NULL)
		*offset = (guint8) tmp;
	g_debug ("using devo offset %u", (guint) tmp);
	return TRUE;
}

/**
 * dfu_cipher_decrypt_devo: (skip)
 * @key: a XTEA key
 * @data: data to parse
 * @length: length of @data
 * @error: a #GError, or %NULL
 *
 * Decrypt a buffer using DEVO obfuscation.
 *
 * Returns: %TRUE for success
 **/
gboolean
dfu_cipher_decrypt_devo (const gchar *key,
			 guint8 *data,
			 guint32 length,
			 GError **error)
{
	guint8 offset;
	guint32 i;

	if (!dfu_tool_parse_devo_key (key, &offset, error))
		return FALSE;

	/* no words for how stupid this cipher is */
	for (i = 0; i < length; i++) {
		guint8 val = data[i];
		if (val >= 0x80 + offset && val <= 0xcf)
			data[i] -= offset;
		else if (val >= 0x80 && val < 0x80 + offset)
			data[i] += (0x50 - offset);
	}

	return TRUE;
}

/**
 * dfu_cipher_encrypt_devo: (skip)
 * @key: a XTEA key
 * @data: data to parse
 * @length: length of @data
 * @error: a #GError, or %NULL
 *
 * Encrypt a buffer using DEVO obfuscation.
 *
 * Returns: %TRUE for success
 **/
gboolean
dfu_cipher_encrypt_devo (const gchar *key,
			 guint8 *data,
			 guint32 length,
			 GError **error)
{
	guint8 offset;
	guint32 i;

	if (!dfu_tool_parse_devo_key (key, &offset, error))
		return FALSE;

	/* no words for how stupid this cipher is */
	for (i = 0; i < length; i++) {
		guint8 val = data[i];
		if (val >= 0x80 && val <= 0xcf - offset)
			data[i] += offset;
		else if (val >= 0xd0 - offset && val < 0xd0)
			data[i] -= (0x50 - offset);
	}

	return TRUE;
}
