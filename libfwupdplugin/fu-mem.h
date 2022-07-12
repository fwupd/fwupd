/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <gio/gio.h>

#include "fu-common.h"

gboolean
fu_memcmp_safe(const guint8 *buf1, gsize bufsz1, const guint8 *buf2, gsize bufsz2, GError **error)
    G_GNUC_WARN_UNUSED_RESULT;
guint8 *
fu_memdup_safe(const guint8 *src, gsize n, GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_memcpy_safe(guint8 *dst,
	       gsize dst_sz,
	       gsize dst_offset,
	       const guint8 *src,
	       gsize src_sz,
	       gsize src_offset,
	       gsize n,
	       GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_memmem_safe(const guint8 *haystack,
	       gsize haystack_sz,
	       const guint8 *needle,
	       gsize needle_sz,
	       gsize *offset,
	       GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_memread_uint8_safe(const guint8 *buf, gsize bufsz, gsize offset, guint8 *value, GError **error)
    G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_memread_uint16_safe(const guint8 *buf,
		       gsize bufsz,
		       gsize offset,
		       guint16 *value,
		       FuEndianType endian,
		       GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_memread_uint24_safe(const guint8 *buf,
		       gsize bufsz,
		       gsize offset,
		       guint32 *value,
		       FuEndianType endian,
		       GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_memread_uint32_safe(const guint8 *buf,
		       gsize bufsz,
		       gsize offset,
		       guint32 *value,
		       FuEndianType endian,
		       GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_memread_uint64_safe(const guint8 *buf,
		       gsize bufsz,
		       gsize offset,
		       guint64 *value,
		       FuEndianType endian,
		       GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_memwrite_uint8_safe(guint8 *buf, gsize bufsz, gsize offset, guint8 value, GError **error)
    G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_memwrite_uint16_safe(guint8 *buf,
			gsize bufsz,
			gsize offset,
			guint16 value,
			FuEndianType endian,
			GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_memwrite_uint32_safe(guint8 *buf,
			gsize bufsz,
			gsize offset,
			guint32 value,
			FuEndianType endian,
			GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_memwrite_uint64_safe(guint8 *buf,
			gsize bufsz,
			gsize offset,
			guint64 value,
			FuEndianType endian,
			GError **error) G_GNUC_WARN_UNUSED_RESULT;

void
fu_memwrite_uint16(guint8 *buf, guint16 val_native, FuEndianType endian);
void
fu_memwrite_uint24(guint8 *buf, guint32 val_native, FuEndianType endian);
void
fu_memwrite_uint32(guint8 *buf, guint32 val_native, FuEndianType endian);
void
fu_memwrite_uint64(guint8 *buf, guint64 val_native, FuEndianType endian);
guint16
fu_memread_uint16(const guint8 *buf, FuEndianType endian);
guint32
fu_memread_uint24(const guint8 *buf, FuEndianType endian);
guint32
fu_memread_uint32(const guint8 *buf, FuEndianType endian);
guint64
fu_memread_uint64(const guint8 *buf, FuEndianType endian);
