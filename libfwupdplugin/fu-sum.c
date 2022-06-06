/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuCommon"

#include "config.h"

#include "fu-mem.h"
#include "fu-sum.h"

/**
 * fu_sum8:
 * @buf: memory buffer
 * @bufsz: size of @buf
 *
 * Returns the arithmetic sum of all bytes in @buf.
 *
 * Returns: sum value
 *
 * Since: 1.8.2
 **/
guint8
fu_sum8(const guint8 *buf, gsize bufsz)
{
	guint8 checksum = 0;
	g_return_val_if_fail(buf != NULL, G_MAXUINT8);
	for (gsize i = 0; i < bufsz; i++)
		checksum += buf[i];
	return checksum;
}

/**
 * fu_sum8_bytes:
 * @blob: a #GBytes
 *
 * Returns the arithmetic sum of all bytes in @blob.
 *
 * Returns: sum value
 *
 * Since: 1.8.2
 **/
guint8
fu_sum8_bytes(GBytes *blob)
{
	g_return_val_if_fail(blob != NULL, G_MAXUINT8);
	if (g_bytes_get_size(blob) == 0)
		return 0;
	return fu_sum8(g_bytes_get_data(blob, NULL), g_bytes_get_size(blob));
}

/**
 * fu_sum16:
 * @buf: memory buffer
 * @bufsz: size of @buf
 *
 * Returns the arithmetic sum of all bytes in @buf, adding them one byte at a time.
 *
 * Returns: sum value
 *
 * Since: 1.8.2
 **/
guint16
fu_sum16(const guint8 *buf, gsize bufsz)
{
	guint16 checksum = 0;
	g_return_val_if_fail(buf != NULL, G_MAXUINT16);
	for (gsize i = 0; i < bufsz; i++)
		checksum += buf[i];
	return checksum;
}

/**
 * fu_sum16_bytes:
 * @blob: a #GBytes
 *
 * Returns the arithmetic sum of all bytes in @blob, adding them one byte at a time.
 *
 * Returns: sum value
 *
 * Since: 1.8.2
 **/
guint16
fu_sum16_bytes(GBytes *blob)
{
	g_return_val_if_fail(blob != NULL, G_MAXUINT16);
	return fu_sum16(g_bytes_get_data(blob, NULL), g_bytes_get_size(blob));
}

/**
 * fu_sum16w:
 * @buf: memory buffer
 * @bufsz: size of @buf
 * @endian: an endian type, e.g. %G_LITTLE_ENDIAN
 *
 * Returns the arithmetic sum of all bytes in @buf, adding them one word at a time.
 * The caller must ensure that @bufsz is a multiple of 2.
 *
 * Returns: sum value
 *
 * Since: 1.8.2
 **/
guint16
fu_sum16w(const guint8 *buf, gsize bufsz, FuEndianType endian)
{
	guint16 checksum = 0;
	g_return_val_if_fail(buf != NULL, G_MAXUINT16);
	g_return_val_if_fail(bufsz % 2 == 0, G_MAXUINT16);
	for (gsize i = 0; i < bufsz; i += 2)
		checksum += fu_memread_uint16(&buf[i], endian);
	return checksum;
}

/**
 * fu_sum16w_bytes:
 * @blob: a #GBytes
 * @endian: an endian type, e.g. %G_LITTLE_ENDIAN
 *
 * Returns the arithmetic sum of all bytes in @blob, adding them one word at a time.
 * The caller must ensure that the size of @blob is a multiple of 2.
 *
 * Returns: sum value
 *
 * Since: 1.8.2
 **/
guint16
fu_sum16w_bytes(GBytes *blob, FuEndianType endian)
{
	g_return_val_if_fail(blob != NULL, G_MAXUINT16);
	return fu_sum16w(g_bytes_get_data(blob, NULL), g_bytes_get_size(blob), endian);
}

/**
 * fu_sum32:
 * @buf: memory buffer
 * @bufsz: size of @buf
 *
 * Returns the arithmetic sum of all bytes in @buf, adding them one byte at a time.
 *
 * Returns: sum value
 *
 * Since: 1.8.2
 **/
guint32
fu_sum32(const guint8 *buf, gsize bufsz)
{
	guint32 checksum = 0;
	g_return_val_if_fail(buf != NULL, G_MAXUINT32);
	for (gsize i = 0; i < bufsz; i++)
		checksum += buf[i];
	return checksum;
}

/**
 * fu_sum32_bytes:
 * @blob: a #GBytes
 *
 * Returns the arithmetic sum of all bytes in @blob, adding them one byte at a time.
 *
 * Returns: sum value
 *
 * Since: 1.8.2
 **/
guint32
fu_sum32_bytes(GBytes *blob)
{
	g_return_val_if_fail(blob != NULL, G_MAXUINT32);
	return fu_sum32(g_bytes_get_data(blob, NULL), g_bytes_get_size(blob));
}

/**
 * fu_sum32w:
 * @buf: memory buffer
 * @bufsz: size of @buf
 * @endian: an endian type, e.g. %G_LITTLE_ENDIAN
 *
 * Returns the arithmetic sum of all bytes in @buf, adding them one dword at a time.
 * The caller must ensure that @bufsz is a multiple of 4.
 *
 * Returns: sum value
 *
 * Since: 1.8.2
 **/
guint32
fu_sum32w(const guint8 *buf, gsize bufsz, FuEndianType endian)
{
	guint32 checksum = 0;
	g_return_val_if_fail(buf != NULL, G_MAXUINT32);
	g_return_val_if_fail(bufsz % 4 == 0, G_MAXUINT32);
	for (gsize i = 0; i < bufsz; i += 4)
		checksum += fu_memread_uint32(&buf[i], endian);
	return checksum;
}

/**
 * fu_sum32w_bytes:
 * @blob: a #GBytes
 * @endian: an endian type, e.g. %G_LITTLE_ENDIAN
 *
 * Returns the arithmetic sum of all bytes in @blob, adding them one dword at a time.
 * The caller must ensure that the size of @blob is a multiple of 4.
 *
 * Returns: sum value
 *
 * Since: 1.8.2
 **/
guint32
fu_sum32w_bytes(GBytes *blob, FuEndianType endian)
{
	g_return_val_if_fail(blob != NULL, G_MAXUINT32);
	return fu_sum32w(g_bytes_get_data(blob, NULL), g_bytes_get_size(blob), endian);
}
