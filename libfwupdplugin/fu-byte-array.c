/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuCommon"

#include "config.h"

#include "fu-byte-array.h"
#include "fu-common.h"
#include "fu-mem.h"

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
	guint8 buf[2];
	fu_memwrite_uint16(buf, data, endian);
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
	guint8 buf[4];
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
	guint8 buf[8];
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
fu_byte_array_set_size(GByteArray *array, guint length, guint8 data)
{
	guint oldlength = array->len;
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
	return fu_memcmp_safe(buf1->data, buf1->len, buf2->data, buf2->len, error);
}
