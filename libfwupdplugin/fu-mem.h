/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupd.h>

#include "fu-endian.h"

/**
 * fu_memcmp_safe:
 * @buf1: a buffer
 * @buf1_sz: sizeof @buf1
 * @buf1_offset: offset into @buf1
 * @buf2: another buffer
 * @buf2_sz: sizeof @buf2
 * @buf2_offset: offset into @buf1
 * @n: number of bytes to compare from @buf1+@buf1_offset from
 * @error: (nullable): optional return location for an error
 *
 * Compares the buffers for equality.
 *
 * Returns: %TRUE if @buf1 and @buf2 are identical
 *
 * Since: 1.8.2
 **/
gboolean
fu_memcmp_safe(const guint8 *buf1,
	       gsize buf1_sz,
	       gsize buf1_offset,
	       const guint8 *buf2,
	       gsize buf2_sz,
	       gsize buf2_offset,
	       gsize n,
	       GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 4);
/**
 * fu_memdup_safe:
 * @src: (nullable): source buffer
 * @n: number of bytes to copy from @src
 * @error: (nullable): optional return location for an error
 *
 * Duplicates some memory using memdup in a safe way.
 *
 * You don't need to use this function in "obviously correct" cases, nor should
 * you use it when performance is a concern. Only us it when you're not sure if
 * malicious data from a device or firmware could cause memory corruption.
 *
 * NOTE: This function intentionally limits allocation size to 1GB.
 *
 * Returns: (transfer full): block of allocated memory, or %NULL for an error.
 *
 * Since: 1.8.2
 **/
guint8 *
fu_memdup_safe(const guint8 *src, gsize n, GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_MALLOC
    G_GNUC_ALLOC_SIZE(2);
/**
 * fu_memcpy_safe:
 * @dst: destination buffer
 * @dst_sz: maximum size of @dst, typically `sizeof(dst)`
 * @dst_offset: offset in bytes into @dst to copy to
 * @src: source buffer
 * @src_sz: maximum size of @dst, typically `sizeof(src)`
 * @src_offset: offset in bytes into @src to copy from
 * @n: number of bytes to copy from @src+@offset from
 * @error: (nullable): optional return location for an error
 *
 * Copies some memory using memcpy in a safe way. Providing the buffer sizes
 * of both the destination and the source allows us to check for buffer overflow.
 *
 * Providing the buffer offsets also allows us to check reading past the end of
 * the source buffer. For this reason the caller should NEVER add an offset to
 * @src or @dst.
 *
 * You don't need to use this function in "obviously correct" cases, nor should
 * you use it when performance is a concern. Only us it when you're not sure if
 * malicious data from a device or firmware could cause memory corruption.
 *
 * Returns: %TRUE if the bytes were copied, %FALSE otherwise
 *
 * Since: 1.8.2
 **/
gboolean
fu_memcpy_safe(guint8 *dst,
	       gsize dst_sz,
	       gsize dst_offset,
	       const guint8 *src,
	       gsize src_sz,
	       gsize src_offset,
	       gsize n,
	       GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 4);
/**
 * fu_memmem_safe:
 * @haystack: destination buffer
 * @haystack_sz: maximum size of @haystack, typically `sizeof(haystack)`
 * @needle: source buffer
 * @needle_sz: maximum size of @haystack, typically `sizeof(needle)`
 * @offset: (out) (nullable): offset in bytes @needle has been found in @haystack
 * @error: (nullable): optional return location for an error
 *
 * Finds a block of memory in another block of memory in a safe way.
 *
 * Returns: %TRUE if the needle was found in the haystack, %FALSE otherwise
 *
 * Since: 1.8.2
 **/
gboolean
fu_memmem_safe(const guint8 *haystack,
	       gsize haystack_sz,
	       const guint8 *needle,
	       gsize needle_sz,
	       gsize *offset,
	       GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 3);
/**
 * fu_memread_uint8_safe:
 * @buf: source buffer
 * @bufsz: maximum size of @buf, typically `sizeof(buf)`
 * @offset: offset in bytes into @buf to copy from
 * @value: (out) (nullable): the parsed value
 * @error: (nullable): optional return location for an error
 *
 * Read a value from a buffer in a safe way.
 *
 * You don't need to use this function in "obviously correct" cases, nor should
 * you use it when performance is a concern. Only us it when you're not sure if
 * malicious data from a device or firmware could cause memory corruption.
 *
 * Returns: %TRUE if @value was set, %FALSE otherwise
 *
 * Since: 1.8.2
 **/
gboolean
fu_memread_uint8_safe(const guint8 *buf, gsize bufsz, gsize offset, guint8 *value, GError **error)
    G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
/**
 * fu_memread_uint16_safe:
 * @buf: source buffer
 * @bufsz: maximum size of @buf, typically `sizeof(buf)`
 * @offset: offset in bytes into @buf to copy from
 * @value: (out) (nullable): the parsed value
 * @endian: an endian type, e.g. %G_LITTLE_ENDIAN
 * @error: (nullable): optional return location for an error
 *
 * Read a value from a buffer using a specified endian in a safe way.
 *
 * You don't need to use this function in "obviously correct" cases, nor should
 * you use it when performance is a concern. Only us it when you're not sure if
 * malicious data from a device or firmware could cause memory corruption.
 *
 * Returns: %TRUE if @value was set, %FALSE otherwise
 *
 * Since: 1.8.2
 **/
gboolean
fu_memread_uint16_safe(const guint8 *buf,
		       gsize bufsz,
		       gsize offset,
		       guint16 *value,
		       FuEndianType endian,
		       GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
/**
 * fu_memread_uint24_safe:
 * @buf: source buffer
 * @bufsz: maximum size of @buf, typically `sizeof(buf)`
 * @offset: offset in bytes into @buf to copy from
 * @value: (out) (nullable): the parsed value
 * @endian: an endian type, e.g. %G_LITTLE_ENDIAN
 * @error: (nullable): optional return location for an error
 *
 * Read a value from a buffer using a specified endian in a safe way.
 *
 * You don't need to use this function in "obviously correct" cases, nor should
 * you use it when performance is a concern. Only us it when you're not sure if
 * malicious data from a device or firmware could cause memory corruption.
 *
 * Returns: %TRUE if @value was set, %FALSE otherwise
 *
 * Since: 1.8.3
 **/
gboolean
fu_memread_uint24_safe(const guint8 *buf,
		       gsize bufsz,
		       gsize offset,
		       guint32 *value,
		       FuEndianType endian,
		       GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
/**
 * fu_memread_uint32_safe:
 * @buf: source buffer
 * @bufsz: maximum size of @buf, typically `sizeof(buf)`
 * @offset: offset in bytes into @buf to copy from
 * @value: (out) (nullable): the parsed value
 * @endian: an endian type, e.g. %G_LITTLE_ENDIAN
 * @error: (nullable): optional return location for an error
 *
 * Read a value from a buffer using a specified endian in a safe way.
 *
 * You don't need to use this function in "obviously correct" cases, nor should
 * you use it when performance is a concern. Only us it when you're not sure if
 * malicious data from a device or firmware could cause memory corruption.
 *
 * Returns: %TRUE if @value was set, %FALSE otherwise
 *
 * Since: 1.8.2
 **/
gboolean
fu_memread_uint32_safe(const guint8 *buf,
		       gsize bufsz,
		       gsize offset,
		       guint32 *value,
		       FuEndianType endian,
		       GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
/**
 * fu_memread_uint64_safe:
 * @buf: source buffer
 * @bufsz: maximum size of @buf, typically `sizeof(buf)`
 * @offset: offset in bytes into @buf to copy from
 * @value: (out) (nullable): the parsed value
 * @endian: an endian type, e.g. %G_LITTLE_ENDIAN
 * @error: (nullable): optional return location for an error
 *
 * Read a value from a buffer using a specified endian in a safe way.
 *
 * You don't need to use this function in "obviously correct" cases, nor should
 * you use it when performance is a concern. Only us it when you're not sure if
 * malicious data from a device or firmware could cause memory corruption.
 *
 * Returns: %TRUE if @value was set, %FALSE otherwise
 *
 * Since: 1.8.2
 **/
gboolean
fu_memread_uint64_safe(const guint8 *buf,
		       gsize bufsz,
		       gsize offset,
		       guint64 *value,
		       FuEndianType endian,
		       GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
/**
 * fu_memwrite_uint8_safe:
 * @buf: source buffer
 * @bufsz: maximum size of @buf, typically `sizeof(buf)`
 * @offset: offset in bytes into @buf to write to
 * @value: the value to write
 * @error: (nullable): optional return location for an error
 *
 * Write a value to a buffer in a safe way.
 *
 * You don't need to use this function in "obviously correct" cases, nor should
 * you use it when performance is a concern. Only us it when you're not sure if
 * malicious data from a device or firmware could cause memory corruption.
 *
 * Returns: %TRUE if @value was written, %FALSE otherwise
 *
 * Since: 1.8.2
 **/
gboolean
fu_memwrite_uint8_safe(guint8 *buf, gsize bufsz, gsize offset, guint8 value, GError **error)
    G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
/**
 * fu_memwrite_uint16_safe:
 * @buf: source buffer
 * @bufsz: maximum size of @buf, typically `sizeof(buf)`
 * @offset: offset in bytes into @buf to write to
 * @value: the value to write
 * @endian: an endian type, e.g. %G_LITTLE_ENDIAN
 * @error: (nullable): optional return location for an error
 *
 * Write a value to a buffer using a specified endian in a safe way.
 *
 * You don't need to use this function in "obviously correct" cases, nor should
 * you use it when performance is a concern. Only us it when you're not sure if
 * malicious data from a device or firmware could cause memory corruption.
 *
 * Returns: %TRUE if @value was written, %FALSE otherwise
 *
 * Since: 1.8.2
 **/
gboolean
fu_memwrite_uint16_safe(guint8 *buf,
			gsize bufsz,
			gsize offset,
			guint16 value,
			FuEndianType endian,
			GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
/**
 * fu_memwrite_uint32_safe:
 * @buf: source buffer
 * @bufsz: maximum size of @buf, typically `sizeof(buf)`
 * @offset: offset in bytes into @buf to write to
 * @value: the value to write
 * @endian: an endian type, e.g. %G_LITTLE_ENDIAN
 * @error: (nullable): optional return location for an error
 *
 * Write a value to a buffer using a specified endian in a safe way.
 *
 * You don't need to use this function in "obviously correct" cases, nor should
 * you use it when performance is a concern. Only us it when you're not sure if
 * malicious data from a device or firmware could cause memory corruption.
 *
 * Returns: %TRUE if @value was written, %FALSE otherwise
 *
 * Since: 1.8.2
 **/
gboolean
fu_memwrite_uint32_safe(guint8 *buf,
			gsize bufsz,
			gsize offset,
			guint32 value,
			FuEndianType endian,
			GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
/**
 * fu_memwrite_uint64_safe:
 * @buf: source buffer
 * @bufsz: maximum size of @buf, typically `sizeof(buf)`
 * @offset: offset in bytes into @buf to write to
 * @value: the value to write
 * @endian: an endian type, e.g. %G_LITTLE_ENDIAN
 * @error: (nullable): optional return location for an error
 *
 * Write a value to a buffer using a specified endian in a safe way.
 *
 * You don't need to use this function in "obviously correct" cases, nor should
 * you use it when performance is a concern. Only us it when you're not sure if
 * malicious data from a device or firmware could cause memory corruption.
 *
 * Returns: %TRUE if @value was written, %FALSE otherwise
 *
 * Since: 1.8.2
 **/
gboolean
fu_memwrite_uint64_safe(guint8 *buf,
			gsize bufsz,
			gsize offset,
			guint64 value,
			FuEndianType endian,
			GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);

/**
 * fu_memwrite_uint16:
 * @buf: a writable buffer
 * @val_native: a value in host byte-order
 * @endian: an endian type, e.g. %G_LITTLE_ENDIAN
 *
 * Writes a value to a buffer using a specified endian.
 *
 * Since: 1.8.2
 **/
void
fu_memwrite_uint16(guint8 *buf, guint16 val_native, FuEndianType endian) G_GNUC_NON_NULL(1);
/**
 * fu_memwrite_uint24:
 * @buf: a writable buffer
 * @val_native: a value in host byte-order
 * @endian: an endian type, e.g. %G_LITTLE_ENDIAN
 *
 * Writes a value to a buffer using a specified endian.
 *
 * Since: 1.8.2
 **/
void
fu_memwrite_uint24(guint8 *buf, guint32 val_native, FuEndianType endian) G_GNUC_NON_NULL(1);
/**
 * fu_memwrite_uint32:
 * @buf: a writable buffer
 * @val_native: a value in host byte-order
 * @endian: an endian type, e.g. %G_LITTLE_ENDIAN
 *
 * Writes a value to a buffer using a specified endian.
 *
 * Since: 1.8.2
 **/
void
fu_memwrite_uint32(guint8 *buf, guint32 val_native, FuEndianType endian) G_GNUC_NON_NULL(1);
/**
 * fu_memwrite_uint64:
 * @buf: a writable buffer
 * @val_native: a value in host byte-order
 * @endian: an endian type, e.g. %G_LITTLE_ENDIAN
 *
 * Writes a value to a buffer using a specified endian.
 *
 * Since: 1.8.2
 **/
void
fu_memwrite_uint64(guint8 *buf, guint64 val_native, FuEndianType endian) G_GNUC_NON_NULL(1);
/**
 * fu_memread_uint16:
 * @buf: a readable buffer
 * @endian: an endian type, e.g. %G_LITTLE_ENDIAN
 *
 * Read a value from a buffer using a specified endian.
 *
 * Returns: a value in host byte-order
 *
 * Since: 1.8.2
 **/
guint16
fu_memread_uint16(const guint8 *buf, FuEndianType endian) G_GNUC_NON_NULL(1);
/**
 * fu_memread_uint24:
 * @buf: a readable buffer
 * @endian: an endian type, e.g. %G_LITTLE_ENDIAN
 *
 * Read a value from a buffer using a specified endian.
 *
 * Returns: a value in host byte-order
 *
 * Since: 1.8.2
 **/
guint32
fu_memread_uint24(const guint8 *buf, FuEndianType endian) G_GNUC_NON_NULL(1);
/**
 * fu_memread_uint32:
 * @buf: a readable buffer
 * @endian: an endian type, e.g. %G_LITTLE_ENDIAN
 *
 * Read a value from a buffer using a specified endian.
 *
 * Returns: a value in host byte-order
 *
 * Since: 1.8.2
 **/
guint32
fu_memread_uint32(const guint8 *buf, FuEndianType endian) G_GNUC_NON_NULL(1);
/**
 * fu_memread_uint64:
 * @buf: a readable buffer
 * @endian: an endian type, e.g. %G_LITTLE_ENDIAN
 *
 * Read a value from a buffer using a specified endian.
 *
 * Returns: a value in host byte-order
 *
 * Since: 1.8.2
 **/
guint64
fu_memread_uint64(const guint8 *buf, FuEndianType endian) G_GNUC_NON_NULL(1);
/**
 * fu_memstrsafe:
 * @buf: source buffer
 * @bufsz: maximum size of @buf, typically `sizeof(buf)`
 * @offset: offset in bytes into @buf to read from
 * @maxsz: maximum size of returned string
 * @error: (nullable): optional return location for an error
 *
 * Converts a byte buffer to a ASCII string.
 *
 * Returns: (transfer full): a string, or %NULL on error
 *
 * Since: 1.9.3
 **/
gchar *
fu_memstrsafe(const guint8 *buf, gsize bufsz, gsize offset, gsize maxsz, GError **error)
    G_GNUC_NON_NULL(1);
