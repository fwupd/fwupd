/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuCommon"

#include "config.h"

#include "fwupd-error.h"

#include "fu-common.h"
#include "fu-mem-private.h"
#include "fu-string.h"

void
fu_memwrite_uint16(guint8 *buf, guint16 val_native, FuEndianType endian)
{
	guint16 val_hw;
	switch (endian) {
	case G_BIG_ENDIAN:
		val_hw = GUINT16_TO_BE(val_native); /* nocheck:blocked */
		break;
	case G_LITTLE_ENDIAN:
		val_hw = GUINT16_TO_LE(val_native); /* nocheck:blocked */
		break;
	default:
		val_hw = val_native;
		break;
	}
	memcpy(buf, &val_hw, sizeof(val_hw)); /* nocheck:blocked */
}

void
fu_memwrite_uint24(guint8 *buf, guint32 val_native, FuEndianType endian)
{
	guint32 val_hw;
	switch (endian) {
	case G_BIG_ENDIAN:
		val_hw = GUINT32_TO_BE(val_native);		   /* nocheck:blocked */
		memcpy(buf, ((const guint8 *)&val_hw) + 0x1, 0x3); /* nocheck:blocked */
		break;
	case G_LITTLE_ENDIAN:
		val_hw = GUINT32_TO_LE(val_native); /* nocheck:blocked */
		memcpy(buf, &val_hw, 0x3);	    /* nocheck:blocked */
		break;
	default:
		g_assert_not_reached();
	}
}

void
fu_memwrite_uint32(guint8 *buf, guint32 val_native, FuEndianType endian)
{
	guint32 val_hw;
	switch (endian) {
	case G_BIG_ENDIAN:
		val_hw = GUINT32_TO_BE(val_native); /* nocheck:blocked */
		break;
	case G_LITTLE_ENDIAN:
		val_hw = GUINT32_TO_LE(val_native); /* nocheck:blocked */
		break;
	default:
		val_hw = val_native;
		break;
	}
	memcpy(buf, &val_hw, sizeof(val_hw)); /* nocheck:blocked */
}

void
fu_memwrite_uint64(guint8 *buf, guint64 val_native, FuEndianType endian)
{
	guint64 val_hw;
	switch (endian) {
	case G_BIG_ENDIAN:
		val_hw = GUINT64_TO_BE(val_native); /* nocheck:blocked */
		break;
	case G_LITTLE_ENDIAN:
		val_hw = GUINT64_TO_LE(val_native); /* nocheck:blocked */
		break;
	default:
		val_hw = val_native;
		break;
	}
	memcpy(buf, &val_hw, sizeof(val_hw)); /* nocheck:blocked */
}

guint16
fu_memread_uint16(const guint8 *buf, FuEndianType endian)
{
	guint16 val_hw, val_native;
	memcpy(&val_hw, buf, sizeof(val_hw)); /* nocheck:blocked */
	switch (endian) {
	case G_BIG_ENDIAN:
		val_native = GUINT16_FROM_BE(val_hw); /* nocheck:blocked */
		break;
	case G_LITTLE_ENDIAN:
		val_native = GUINT16_FROM_LE(val_hw); /* nocheck:blocked */
		break;
	default:
		val_native = val_hw;
		break;
	}
	return val_native;
}

guint32
fu_memread_uint24(const guint8 *buf, FuEndianType endian)
{
	guint32 val_hw = 0;
	guint32 val_native;
	switch (endian) {
	case G_BIG_ENDIAN:
		memcpy(((guint8 *)&val_hw) + 0x1, buf, 0x3); /* nocheck:blocked */
		val_native = GUINT32_FROM_BE(val_hw);	     /* nocheck:blocked */
		break;
	case G_LITTLE_ENDIAN:
		memcpy(&val_hw, buf, 0x3);	      /* nocheck:blocked */
		val_native = GUINT32_FROM_LE(val_hw); /* nocheck:blocked */
		break;
	default:
		val_native = val_hw;
		break;
	}
	return val_native;
}

guint32
fu_memread_uint32(const guint8 *buf, FuEndianType endian)
{
	guint32 val_hw, val_native;
	memcpy(&val_hw, buf, sizeof(val_hw)); /* nocheck:blocked */
	switch (endian) {
	case G_BIG_ENDIAN:
		val_native = GUINT32_FROM_BE(val_hw); /* nocheck:blocked */
		break;
	case G_LITTLE_ENDIAN:
		val_native = GUINT32_FROM_LE(val_hw); /* nocheck:blocked */
		break;
	default:
		val_native = val_hw;
		break;
	}
	return val_native;
}

guint64
fu_memread_uint64(const guint8 *buf, FuEndianType endian)
{
	guint64 val_hw, val_native;
	memcpy(&val_hw, buf, sizeof(val_hw)); /* nocheck:blocked */
	switch (endian) {
	case G_BIG_ENDIAN:
		val_native = GUINT64_FROM_BE(val_hw); /* nocheck:blocked */
		break;
	case G_LITTLE_ENDIAN:
		val_native = GUINT64_FROM_LE(val_hw); /* nocheck:blocked */
		break;
	default:
		val_native = val_hw;
		break;
	}
	return val_native;
}

gboolean
fu_memcmp_safe(const guint8 *buf1,
	       gsize buf1_sz,
	       gsize buf1_offset,
	       const guint8 *buf2,
	       gsize buf2_sz,
	       gsize buf2_offset,
	       gsize n,
	       GError **error)
{
	g_return_val_if_fail(buf1 != NULL, FALSE);
	g_return_val_if_fail(buf2 != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (!fu_memchk_read(buf1_sz, buf1_offset, n, error))
		return FALSE;
	if (!fu_memchk_read(buf2_sz, buf2_offset, n, error))
		return FALSE;

	/* check matches */
	for (guint i = 0x0; i < n; i++) {
		if (buf1[buf1_offset + i] != buf2[buf2_offset + i]) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "got 0x%02x, expected 0x%02x @ 0x%04x",
				    buf1[buf1_offset + i],
				    buf2[buf2_offset + i],
				    i);
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

gboolean
fu_memchk_read(gsize bufsz, gsize offset, gsize n, GError **error)
{
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	if (n == 0)
		return TRUE;
	if (n > bufsz) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_READ,
			    "attempted to read 0x%02x bytes from buffer of 0x%02x",
			    (guint)n,
			    (guint)bufsz);
		return FALSE;
	}
	if (fu_size_checked_add(offset, n) == G_MAXSIZE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_READ,
			    "offset 0x%02x + 0x%02x overflowed",
			    (guint)offset,
			    (guint)n);
		return FALSE;
	}
	if (offset > bufsz || n + offset > bufsz) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_READ,
			    "attempted to read 0x%02x bytes at offset 0x%02x from buffer of 0x%02x",
			    (guint)n,
			    (guint)offset,
			    (guint)bufsz);
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_memchk_write(gsize bufsz, gsize offset, gsize n, GError **error)
{
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	if (n == 0)
		return TRUE;
	if (n > bufsz) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "attempted to write 0x%02x bytes to buffer of 0x%02x",
			    (guint)n,
			    (guint)bufsz);
		return FALSE;
	}
	if (fu_size_checked_add(offset, n) == G_MAXSIZE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "offset 0x%02x + 0x%02x overflowed",
			    (guint)offset,
			    (guint)n);
		return FALSE;
	}
	if (offset > bufsz || n + offset > bufsz) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "attempted to write 0x%02x bytes at offset 0x%02x to buffer of 0x%02x",
			    (guint)n,
			    (guint)offset,
			    (guint)bufsz);
		return FALSE;
	}
	return TRUE;
}

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

	if (!fu_memchk_read(src_sz, src_offset, n, error))
		return FALSE;
	if (!fu_memchk_write(dst_sz, dst_offset, n, error))
		return FALSE;
	memcpy(dst + dst_offset, src + src_offset, n); /* nocheck:blocked */
	return TRUE;
}

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

guint8 *
fu_memdup_safe(const guint8 *src, gsize n, GError **error)
{
	/* sanity check */
	if (n > 0x40000000) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "cannot allocate %uGB of memory",
			    (guint)(n / 0x40000000));
		return NULL;
	}

	/* linear block of memory */
	return g_memdup2(src, n);
}

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

gchar *
fu_memstrsafe(const guint8 *buf, gsize bufsz, gsize offset, gsize maxsz, GError **error)
{
	g_autofree gchar *str = NULL;

	g_return_val_if_fail(buf != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	if (!fu_memchk_read(bufsz, offset, maxsz, error))
		return NULL;
	/* nocheck:memread */
	str = fu_strsafe((const gchar *)buf + offset, maxsz);
	if (str == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "invalid ASCII string");
		return NULL;
	}
	return g_steal_pointer(&str);
}
