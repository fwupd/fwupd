/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupd.h>

#include "fu-crc.h"
#include "fu-endian.h"

GInputStream *
fu_input_stream_from_path(const gchar *path, GError **error) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1);
gboolean
fu_input_stream_size(GInputStream *stream, gsize *val, GError **error) G_GNUC_NON_NULL(1);
gboolean
fu_input_stream_read_safe(GInputStream *stream,
			  guint8 *buf,
			  gsize bufsz,
			  gsize offset,
			  gsize seek_set,
			  gsize count,
			  GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 2);
gboolean
fu_input_stream_read_u8(GInputStream *stream, gsize offset, guint8 *value, GError **error)
    G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 3);
gboolean
fu_input_stream_read_u16(GInputStream *stream,
			 gsize offset,
			 guint16 *value,
			 FuEndianType endian,
			 GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 3);
gboolean
fu_input_stream_read_u24(GInputStream *stream,
			 gsize offset,
			 guint32 *value,
			 FuEndianType endian,
			 GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 3);
gboolean
fu_input_stream_read_u32(GInputStream *stream,
			 gsize offset,
			 guint32 *value,
			 FuEndianType endian,
			 GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 3);
gboolean
fu_input_stream_read_u64(GInputStream *stream,
			 gsize offset,
			 guint64 *value,
			 FuEndianType endian,
			 GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 3);
GByteArray *
fu_input_stream_read_byte_array(GInputStream *stream, gsize offset, gsize count, GError **error)
    G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
GBytes *
fu_input_stream_read_bytes(GInputStream *stream, gsize offset, gsize count, GError **error)
    G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);

typedef gboolean (*FuInputStreamChunkifyFunc)(const guint8 *buf,
					      gsize bufsz,
					      gpointer user_data,
					      GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_input_stream_chunkify(GInputStream *stream,
			 FuInputStreamChunkifyFunc func_cb,
			 gpointer user_data,
			 GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 2);

gboolean
fu_input_stream_compute_sum8(GInputStream *stream,
			     guint8 *value,
			     GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 2);
gboolean
fu_input_stream_compute_sum16(GInputStream *stream,
			      guint16 *value,
			      GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 2);
gboolean
fu_input_stream_compute_crc16(GInputStream *stream, FuCrcKind kind, guint16 *crc, GError **error)
    G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 3);
gboolean
fu_input_stream_compute_crc32(GInputStream *stream, FuCrcKind kind, guint32 *crc, GError **error)
    G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 3);
gchar *
fu_input_stream_compute_checksum(GInputStream *stream,
				 GChecksumType checksum_type,
				 GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
gboolean
fu_input_stream_find(GInputStream *stream,
		     const guint8 *buf,
		     gsize bufsz,
		     gsize *offset,
		     GError **error) G_GNUC_NON_NULL(1, 2);
