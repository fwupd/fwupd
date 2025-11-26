/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuCommon"

#include "config.h"

#include "fu-byte-array.h"
#include "fu-common.h"
#include "fu-firmware-common.h"
#include "fu-mem.h"

/**
 * fu_byte_array_to_string:
 * @array: a #GByteArray
 *
 * Converts the byte array to a lowercase hex string.
 *
 * Returns: (transfer full): a string, which may be zero length
 *
 * Since: 1.8.9
 **/
gchar *
fu_byte_array_to_string(GByteArray *array)
{
	g_autoptr(GString) str = g_string_new(NULL);
	g_return_val_if_fail(array != NULL, NULL);
	for (guint i = 0; i < array->len; i++)
		g_string_append_printf(str, "%02x", array->data[i]);
	return g_string_free(g_steal_pointer(&str), FALSE);
}

/**
 * fu_byte_array_from_string:
 * @str: a hex string
 * @error: (nullable): optional return location for an error
 *
 * Converts a lowercase hex string to a byte array.
 *
 * Returns: (transfer full): a #GByteArray, or %NULL on error
 *
 * Since: 1.9.6
 **/
GByteArray *
fu_byte_array_from_string(const gchar *str, GError **error)
{
	gsize strsz;
	g_autoptr(GByteArray) buf = g_byte_array_new();

	g_return_val_if_fail(str != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	strsz = strlen(str);
	for (guint i = 0; i < strsz; i += 2) {
		guint8 value = 0;
		if (!fu_firmware_strparse_uint8_safe(str, strsz, i, &value, error))
			return NULL;
		fu_byte_array_append_uint8(buf, value);
	}
	return g_steal_pointer(&buf);
}

/**
 * fu_byte_array_append_uint8:
 * @array: a #GByteArray
 * @data: value
 *
 * Adds a 8 bit integer to a byte array.
 *
 * Since: 1.3.1
 **/
void
fu_byte_array_append_uint8(GByteArray *array, guint8 data)
{
	g_byte_array_append(array, &data, sizeof(data));
}

/**
 * fu_byte_array_append_uint16:
 * @array: a #GByteArray
 * @data: value
 * @endian: endian type, e.g. #G_LITTLE_ENDIAN
 *
 * Adds a 16 bit integer to a byte array.
 *
 * Since: 1.3.1
 **/
void
fu_byte_array_append_uint16(GByteArray *array, guint16 data, FuEndianType endian)
{
	guint8 buf[2]; /* nocheck:zero-init */
	fu_memwrite_uint16(buf, data, endian);
	g_byte_array_append(array, buf, sizeof(buf));
}

/**
 * fu_byte_array_append_uint24:
 * @array: a #GByteArray
 * @data: value
 * @endian: endian type, e.g. #G_LITTLE_ENDIAN
 *
 * Adds a 24 bit integer to a byte array.
 *
 * Since: 1.8.13
 **/
void
fu_byte_array_append_uint24(GByteArray *array, guint32 data, FuEndianType endian)
{
	guint8 buf[3]; /* nocheck:zero-init */
	fu_memwrite_uint24(buf, data, endian);
	g_byte_array_append(array, buf, sizeof(buf));
}

/**
 * fu_byte_array_append_uint32:
 * @array: a #GByteArray
 * @data: value
 * @endian: endian type, e.g. #G_LITTLE_ENDIAN
 *
 * Adds a 32 bit integer to a byte array.
 *
 * Since: 1.3.1
 **/
void
fu_byte_array_append_uint32(GByteArray *array, guint32 data, FuEndianType endian)
{
	guint8 buf[4]; /* nocheck:zero-init */
	fu_memwrite_uint32(buf, data, endian);
	g_byte_array_append(array, buf, sizeof(buf));
}

/**
 * fu_byte_array_append_uint64:
 * @array: a #GByteArray
 * @data: value
 * @endian: endian type, e.g. #G_LITTLE_ENDIAN
 *
 * Adds a 64 bit integer to a byte array.
 *
 * Since: 1.5.8
 **/
void
fu_byte_array_append_uint64(GByteArray *array, guint64 data, FuEndianType endian)
{
	guint8 buf[8]; /* nocheck:zero-init */
	fu_memwrite_uint64(buf, data, endian);
	g_byte_array_append(array, buf, sizeof(buf));
}

/**
 * fu_byte_array_append_bytes:
 * @array: a #GByteArray
 * @bytes: data blob
 *
 * Adds the contents of a GBytes to a byte array.
 *
 * Since: 1.5.8
 **/
void
fu_byte_array_append_bytes(GByteArray *array, GBytes *bytes)
{
	g_byte_array_append(array, g_bytes_get_data(bytes, NULL), g_bytes_get_size(bytes));
}

/**
 * fu_byte_array_set_size:
 * @array: a #GByteArray
 * @length:  the new size of the GByteArray
 * @data: the byte used to pad the array
 *
 * Sets the size of the GByteArray, expanding with @data as required.
 *
 * Since: 1.8.2
 **/
void
fu_byte_array_set_size(GByteArray *array, gsize length, guint8 data)
{
	guint oldlength = array->len;
	g_return_if_fail(array != NULL);
	g_return_if_fail(length < G_MAXUINT);
	g_byte_array_set_size(array, length);
	if (length > oldlength)
		memset(array->data + oldlength, data, length - oldlength);
}

/**
 * fu_byte_array_align_up:
 * @array: a #GByteArray
 * @alignment: align to this power of 2
 * @data: the byte used to pad the array
 *
 * Align a byte array length to a power of 2 boundary, where @alignment is the
 * bit position to align to. If @alignment is zero then @array is unchanged.
 *
 * Since: 1.6.0
 **/
void
fu_byte_array_align_up(GByteArray *array, guint8 alignment, guint8 data)
{
	fu_byte_array_set_size(array, fu_common_align_up(array->len, alignment), data);
}

/**
 * fu_byte_array_compare:
 * @buf1: a data blob
 * @buf2: another #GByteArray
 * @error: (nullable): optional return location for an error
 *
 * Compares two buffers for equality.
 *
 * Returns: %TRUE if @buf1 and @buf2 are identical
 *
 * Since: 1.8.0
 **/
gboolean
fu_byte_array_compare(GByteArray *buf1, GByteArray *buf2, GError **error)
{
	g_return_val_if_fail(buf1 != NULL, FALSE);
	g_return_val_if_fail(buf2 != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	return fu_memcmp_safe(buf1->data,
			      buf1->len,
			      0x0,
			      buf2->data,
			      buf2->len,
			      0x0,
			      MAX(buf1->len, buf2->len),
			      error);
}
