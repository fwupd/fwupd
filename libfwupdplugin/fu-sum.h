/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-endian.h"

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
fu_sum8(const guint8 *buf, gsize bufsz);
/**
 * fu_sum8_safe:
 * @buf: source buffer
 * @bufsz: maximum size of @buf, typically `sizeof(buf)`
 * @offset: offset in bytes into @buf where sum should start
 * @n: number of bytes to sum from @buf
 * @value: (out) (nullable): the result
 * @error: (nullable): optional return location for an error
 *
 * Returns the arithmetic sum of all bytes in @buf.
 *
 * You don't need to use this function in "obviously correct" cases, nor should
 * you use it when performance is a concern. Only use it when you're not sure if
 * malicious data from a device or firmware could cause memory corruption.
 *
 * Returns: %TRUE on success, %FALSE otherwise
 *
 * Since: 2.1.2
 **/
gboolean
fu_sum8_safe(const guint8 *buf, gsize bufsz, gsize offset, gsize n, guint8 *value, GError **error)
    G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
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
fu_sum8_bytes(GBytes *blob);
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
fu_sum16(const guint8 *buf, gsize bufsz);
/**
 * fu_sum16_safe:
 * @buf: source buffer
 * @bufsz: maximum size of @buf, typically `sizeof(buf)`
 * @offset: offset in bytes into @buf where sum should start
 * @n: number of bytes to sum from @buf
 * @value: (out) (nullable): the result
 * @error: (nullable): optional return location for an error
 *
 * Returns the arithmetic sum of all bytes in @buf, adding them one byte at a time.
 *
 * You don't need to use this function in "obviously correct" cases, nor should
 * you use it when performance is a concern. Only use it when you're not sure if
 * malicious data from a device or firmware could cause memory corruption.
 *
 * Returns: %TRUE on success, %FALSE otherwise
 *
 * Since: 2.1.2
 **/
gboolean
fu_sum16_safe(const guint8 *buf, gsize bufsz, gsize offset, gsize n, guint16 *value, GError **error)
    G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
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
fu_sum16_bytes(GBytes *blob);
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
fu_sum16w(const guint8 *buf, gsize bufsz, FuEndianType endian);
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
fu_sum16w_bytes(GBytes *blob, FuEndianType endian);
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
fu_sum32(const guint8 *buf, gsize bufsz);
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
fu_sum32_bytes(GBytes *blob);
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
fu_sum32w(const guint8 *buf, gsize bufsz, FuEndianType endian);
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
fu_sum32w_bytes(GBytes *blob, FuEndianType endian);
