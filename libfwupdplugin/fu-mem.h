/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupd.h>

#include "fu-endian.h"

gboolean
fu_memcmp_safe(const guint8 *buf1,
	       gsize buf1_sz,
	       gsize buf1_offset,
	       const guint8 *buf2,
	       gsize buf2_sz,
	       gsize buf2_offset,
	       gsize n,
	       GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 4);
guint8 *
fu_memdup_safe(const guint8 *src, gsize n, GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_MALLOC
    G_GNUC_ALLOC_SIZE(2);
gboolean
fu_memcpy_safe(guint8 *dst,
	       gsize dst_sz,
	       gsize dst_offset,
	       const guint8 *src,
	       gsize src_sz,
	       gsize src_offset,
	       gsize n,
	       GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 4);
gboolean
fu_memmem_safe(const guint8 *haystack,
	       gsize haystack_sz,
	       const guint8 *needle,
	       gsize needle_sz,
	       gsize *offset,
	       GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 3);
gboolean
fu_memread_uint8_safe(const guint8 *buf, gsize bufsz, gsize offset, guint8 *value, GError **error)
    G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
gboolean
fu_memread_uint16_safe(const guint8 *buf,
		       gsize bufsz,
		       gsize offset,
		       guint16 *value,
		       FuEndianType endian,
		       GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
gboolean
fu_memread_uint24_safe(const guint8 *buf,
		       gsize bufsz,
		       gsize offset,
		       guint32 *value,
		       FuEndianType endian,
		       GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
gboolean
fu_memread_uint32_safe(const guint8 *buf,
		       gsize bufsz,
		       gsize offset,
		       guint32 *value,
		       FuEndianType endian,
		       GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
gboolean
fu_memread_uint64_safe(const guint8 *buf,
		       gsize bufsz,
		       gsize offset,
		       guint64 *value,
		       FuEndianType endian,
		       GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
gboolean
fu_memwrite_uint8_safe(guint8 *buf, gsize bufsz, gsize offset, guint8 value, GError **error)
    G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
gboolean
fu_memwrite_uint16_safe(guint8 *buf,
			gsize bufsz,
			gsize offset,
			guint16 value,
			FuEndianType endian,
			GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
gboolean
fu_memwrite_uint32_safe(guint8 *buf,
			gsize bufsz,
			gsize offset,
			guint32 value,
			FuEndianType endian,
			GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
gboolean
fu_memwrite_uint64_safe(guint8 *buf,
			gsize bufsz,
			gsize offset,
			guint64 value,
			FuEndianType endian,
			GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);

void
fu_memwrite_uint16(guint8 *buf, guint16 val_native, FuEndianType endian) G_GNUC_NON_NULL(1);
void
fu_memwrite_uint24(guint8 *buf, guint32 val_native, FuEndianType endian) G_GNUC_NON_NULL(1);
void
fu_memwrite_uint32(guint8 *buf, guint32 val_native, FuEndianType endian) G_GNUC_NON_NULL(1);
void
fu_memwrite_uint64(guint8 *buf, guint64 val_native, FuEndianType endian) G_GNUC_NON_NULL(1);
guint16
fu_memread_uint16(const guint8 *buf, FuEndianType endian) G_GNUC_NON_NULL(1);
guint32
fu_memread_uint24(const guint8 *buf, FuEndianType endian) G_GNUC_NON_NULL(1);
guint32
fu_memread_uint32(const guint8 *buf, FuEndianType endian) G_GNUC_NON_NULL(1);
guint64
fu_memread_uint64(const guint8 *buf, FuEndianType endian) G_GNUC_NON_NULL(1);
gchar *
fu_memstrsafe(const guint8 *buf, gsize bufsz, gsize offset, gsize maxsz, GError **error)
    G_GNUC_NON_NULL(1);
