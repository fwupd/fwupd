/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuCommon"

#include "config.h"

#include "fwupd-error.h"

#include "fu-mem.h"

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
fu_memwrite_uint16(guint8 *buf, guint16 val_native, FuEndianType endian)
{
	guint16 val_hw;
	switch (endian) {
	case G_BIG_ENDIAN:
		val_hw = GUINT16_TO_BE(val_native);
		break;
	case G_LITTLE_ENDIAN:
		val_hw = GUINT16_TO_LE(val_native);
		break;
	default:
		g_assert_not_reached();
	}
	memcpy(buf, &val_hw, sizeof(val_hw));
}

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
fu_memwrite_uint24(guint8 *buf, guint32 val_native, FuEndianType endian)
{
	guint32 val_hw;
	switch (endian) {
	case G_BIG_ENDIAN:
		val_hw = GUINT32_TO_BE(val_native);
		memcpy(buf, ((const guint8 *)&val_hw) + 0x1, 0x3);
		break;
	case G_LITTLE_ENDIAN:
		val_hw = GUINT32_TO_LE(val_native);
		memcpy(buf, &val_hw, 0x3);
		break;
	default:
		g_assert_not_reached();
	}
}

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
fu_memwrite_uint32(guint8 *buf, guint32 val_native, FuEndianType endian)
{
	guint32 val_hw;
	switch (endian) {
	case G_BIG_ENDIAN:
		val_hw = GUINT32_TO_BE(val_native);
		break;
	case G_LITTLE_ENDIAN:
		val_hw = GUINT32_TO_LE(val_native);
		break;
	default:
		g_assert_not_reached();
	}
	memcpy(buf, &val_hw, sizeof(val_hw));
}

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
fu_memwrite_uint64(guint8 *buf, guint64 val_native, FuEndianType endian)
{
	guint64 val_hw;
	switch (endian) {
	case G_BIG_ENDIAN:
		val_hw = GUINT64_TO_BE(val_native);
		break;
	case G_LITTLE_ENDIAN:
		val_hw = GUINT64_TO_LE(val_native);
		break;
	default:
		g_assert_not_reached();
	}
	memcpy(buf, &val_hw, sizeof(val_hw));
}

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
fu_memread_uint16(const guint8 *buf, FuEndianType endian)
{
	guint16 val_hw, val_native;
	memcpy(&val_hw, buf, sizeof(val_hw));
	switch (endian) {
	case G_BIG_ENDIAN:
		val_native = GUINT16_FROM_BE(val_hw);
		break;
	case G_LITTLE_ENDIAN:
		val_native = GUINT16_FROM_LE(val_hw);
		break;
	default:
		g_assert_not_reached();
	}
	return val_native;
}

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
fu_memread_uint24(const guint8 *buf, FuEndianType endian)
{
	guint32 val_hw = 0;
	guint32 val_native;
	switch (endian) {
	case G_BIG_ENDIAN:
		memcpy(((guint8 *)&val_hw) + 0x1, buf, 0x3);
		val_native = GUINT32_FROM_BE(val_hw);
		break;
	case G_LITTLE_ENDIAN:
		memcpy(&val_hw, buf, 0x3);
		val_native = GUINT32_FROM_LE(val_hw);
		break;
	default:
		g_assert_not_reached();
	}
	return val_native;
}

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
fu_memread_uint32(const guint8 *buf, FuEndianType endian)
{
	guint32 val_hw, val_native;
	memcpy(&val_hw, buf, sizeof(val_hw));
	switch (endian) {
	case G_BIG_ENDIAN:
		val_native = GUINT32_FROM_BE(val_hw);
		break;
	case G_LITTLE_ENDIAN:
		val_native = GUINT32_FROM_LE(val_hw);
		break;
	default:
		g_assert_not_reached();
	}
	return val_native;
}

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
fu_memread_uint64(const guint8 *buf, FuEndianType endian)
{
	guint64 val_hw, val_native;
	memcpy(&val_hw, buf, sizeof(val_hw));
	switch (endian) {
	case G_BIG_ENDIAN:
		val_native = GUINT64_FROM_BE(val_hw);
		break;
	case G_LITTLE_ENDIAN:
		val_native = GUINT64_FROM_LE(val_hw);
		break;
	default:
		g_assert_not_reached();
	}
	return val_native;
}

/**
 * fu_memcmp_safe:
 * @buf1: a buffer
 * @bufsz1: sizeof @buf1
 * @buf2: another buffer
 * @bufsz2: sizeof @buf2
 * @error: (nullable): optional return location for an error
 *
 * Compares the buffers for equality.
 *
 * Returns: %TRUE if @buf1 and @buf2 are identical
 *
 * Since: 1.8.2
 **/
gboolean
fu_memcmp_safe(const guint8 *buf1, gsize bufsz1, const guint8 *buf2, gsize bufsz2, GError **error)
{
	g_return_val_if_fail(buf1 != NULL, FALSE);
	g_return_val_if_fail(buf2 != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* not the same length */
	if (bufsz1 != bufsz2) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "got %" G_GSIZE_FORMAT " bytes, expected "
			    "%" G_GSIZE_FORMAT,
			    bufsz1,
			    bufsz2);
		return FALSE;
	}

	/* check matches */
	for (guint i = 0x0; i < bufsz1; i++) {
		if (buf1[i] != buf2[i]) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "got 0x%02x, expected 0x%02x @ 0x%04x",
				    buf1[i],
				    buf2[i],
				    i);
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

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
	       GError **error)
{
	g_return_val_if_fail(dst != NULL, FALSE);
	g_return_val_if_fail(src != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (n == 0)
		return TRUE;

	if (n > src_sz) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_READ,
			    "attempted to read 0x%02x bytes from buffer of 0x%02x",
			    (guint)n,
			    (guint)src_sz);
		return FALSE;
	}
	if (src_offset > src_sz || n + src_offset > src_sz) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_READ,
			    "attempted to read 0x%02x bytes at offset 0x%02x from buffer of 0x%02x",
			    (guint)n,
			    (guint)src_offset,
			    (guint)src_sz);
		return FALSE;
	}
	if (n > dst_sz) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "attempted to write 0x%02x bytes to buffer of 0x%02x",
			    (guint)n,
			    (guint)dst_sz);
		return FALSE;
	}
	if (dst_offset > dst_sz || n + dst_offset > dst_sz) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "attempted to write 0x%02x bytes at offset 0x%02x to buffer of 0x%02x",
			    (guint)n,
			    (guint)dst_offset,
			    (guint)dst_sz);
		return FALSE;
	}

	/* phew! */
	memcpy(dst + dst_offset, src + src_offset, n);
	return TRUE;
}

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
	       GError **error)
{
#ifdef HAVE_MEMMEM
	const guint8 *tmp;
#endif
	g_return_val_if_fail(haystack != NULL, FALSE);
	g_return_val_if_fail(needle != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* nothing to find */
	if (needle_sz == 0) {
		if (offset != NULL)
			*offset = 0;
		return TRUE;
	}

	/* impossible */
	if (needle_sz > haystack_sz) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "needle of 0x%02x bytes is larger than haystack of 0x%02x bytes",
			    (guint)needle_sz,
			    (guint)haystack_sz);
		return FALSE;
	}

#ifdef HAVE_MEMMEM
	/* trust glibc to do a binary or linear search as appropriate */
	tmp = memmem(haystack, haystack_sz, needle, needle_sz);
	if (tmp != NULL) {
		if (offset != NULL)
			*offset = tmp - haystack;
		return TRUE;
	}
#else
	for (gsize i = 0; i < haystack_sz - needle_sz; i++) {
		if (memcmp(haystack + i, needle, needle_sz) == 0) {
			if (offset != NULL)
				*offset = i;
			return TRUE;
		}
	}
#endif

	/* not found */
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_FOUND,
		    "needle of 0x%02x bytes was not found in haystack of 0x%02x bytes",
		    (guint)needle_sz,
		    (guint)haystack_sz);
	return FALSE;
}

/**
 * fu_memdup_safe:
 * @src: source buffer
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
fu_memdup_safe(const guint8 *src, gsize n, GError **error)
{
	/* sanity check */
	if (n > 0x40000000) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "cannot allocate %uGB of memory",
			    (guint)(n / 0x40000000));
		return NULL;
	}

#if GLIB_CHECK_VERSION(2, 67, 3)
	/* linear block of memory */
	return g_memdup2(src, n);
#else
	return g_memdup(src, (guint)n);
#endif
}

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
{
	guint8 tmp;

	g_return_val_if_fail(buf != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (!fu_memcpy_safe(&tmp,
			    sizeof(tmp),
			    0x0, /* dst */
			    buf,
			    bufsz,
			    offset, /* src */
			    sizeof(tmp),
			    error))
		return FALSE;
	if (value != NULL)
		*value = tmp;
	return TRUE;
}

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
		       GError **error)
{
	guint8 dst[2] = {0x0};

	g_return_val_if_fail(buf != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (!fu_memcpy_safe(dst,
			    sizeof(dst),
			    0x0, /* dst */
			    buf,
			    bufsz,
			    offset, /* src */
			    sizeof(dst),
			    error))
		return FALSE;
	if (value != NULL)
		*value = fu_memread_uint16(dst, endian);
	return TRUE;
}

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
		       GError **error)
{
	guint8 dst[3] = {0x0};

	g_return_val_if_fail(buf != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (!fu_memcpy_safe(dst,
			    sizeof(dst),
			    0x0, /* dst */
			    buf,
			    bufsz,
			    offset, /* src */
			    sizeof(dst),
			    error))
		return FALSE;
	if (value != NULL)
		*value = fu_memread_uint24(dst, endian);
	return TRUE;
}

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
		       GError **error)
{
	guint8 dst[4] = {0x0};

	g_return_val_if_fail(buf != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (!fu_memcpy_safe(dst,
			    sizeof(dst),
			    0x0, /* dst */
			    buf,
			    bufsz,
			    offset, /* src */
			    sizeof(dst),
			    error))
		return FALSE;
	if (value != NULL)
		*value = fu_memread_uint32(dst, endian);
	return TRUE;
}

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
		       GError **error)
{
	guint8 dst[8] = {0x0};

	g_return_val_if_fail(buf != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (!fu_memcpy_safe(dst,
			    sizeof(dst),
			    0x0, /* dst */
			    buf,
			    bufsz,
			    offset, /* src */
			    sizeof(dst),
			    error))
		return FALSE;
	if (value != NULL)
		*value = fu_memread_uint64(dst, endian);
	return TRUE;
}

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
{
	g_return_val_if_fail(buf != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	return fu_memcpy_safe(buf,
			      bufsz,
			      offset, /* dst */
			      &value,
			      sizeof(value),
			      0x0, /* src */
			      sizeof(value),
			      error);
}

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
			GError **error)
{
	guint8 tmp[2] = {0x0};

	g_return_val_if_fail(buf != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	fu_memwrite_uint16(tmp, value, endian);
	return fu_memcpy_safe(buf,
			      bufsz,
			      offset, /* dst */
			      tmp,
			      sizeof(tmp),
			      0x0, /* src */
			      sizeof(tmp),
			      error);
}

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
			GError **error)
{
	guint8 tmp[4] = {0x0};

	g_return_val_if_fail(buf != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	fu_memwrite_uint32(tmp, value, endian);
	return fu_memcpy_safe(buf,
			      bufsz,
			      offset, /* dst */
			      tmp,
			      sizeof(tmp),
			      0x0, /* src */
			      sizeof(tmp),
			      error);
}

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
			GError **error)
{
	guint8 tmp[8] = {0x0};

	g_return_val_if_fail(buf != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	fu_memwrite_uint64(tmp, value, endian);
	return fu_memcpy_safe(buf,
			      bufsz,
			      offset, /* dst */
			      tmp,
			      sizeof(tmp),
			      0x0, /* src */
			      sizeof(tmp),
			      error);
}
