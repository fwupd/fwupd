/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupd.h>

#include "fu-crc.h"
#include "fu-endian.h"
#include "fu-progress.h"

typedef GInputStream FuInputStream;	      /* nocheck:blocked */
typedef GInputStreamClass FuInputStreamClass; /* nocheck:blocked */

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuInputStream, g_object_unref) /* nocheck:blocked */
#define FU_INPUT_STREAM	      G_INPUT_STREAM		     /* nocheck:blocked */
#define FU_TYPE_INPUT_STREAM  G_TYPE_INPUT_STREAM	     /* nocheck:blocked */
#define FU_IS_INPUT_STREAM    G_IS_INPUT_STREAM		     /* nocheck:blocked */
#define FU_INPUT_STREAM_CLASS G_INPUT_STREAM_CLASS	     /* nocheck:blocked */

FuInputStream *
fu_input_stream_from_path(const gchar *path, GError **error) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1);
gboolean
fu_input_stream_size(FuInputStream *stream, gsize *val, GError **error) G_GNUC_NON_NULL(1);
gboolean
fu_input_stream_read_safe(FuInputStream *stream,
			  guint8 *buf,
			  gsize bufsz,
			  gsize offset,
			  gsize seek_set,
			  gsize count,
			  GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 2);
gboolean
fu_input_stream_read_u8(FuInputStream *stream, gsize offset, guint8 *value, GError **error)
    G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 3);
gboolean
fu_input_stream_read_u16(FuInputStream *stream,
			 gsize offset,
			 guint16 *value,
			 FuEndianType endian,
			 GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 3);
gboolean
fu_input_stream_read_u24(FuInputStream *stream,
			 gsize offset,
			 guint32 *value,
			 FuEndianType endian,
			 GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 3);
gboolean
fu_input_stream_read_u32(FuInputStream *stream,
			 gsize offset,
			 guint32 *value,
			 FuEndianType endian,
			 GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 3);
gboolean
fu_input_stream_read_u64(FuInputStream *stream,
			 gsize offset,
			 guint64 *value,
			 FuEndianType endian,
			 GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 3);
GByteArray *
fu_input_stream_read_byte_array(FuInputStream *stream,
				gsize offset,
				gsize count,
				FuProgress *progress,
				GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
GBytes *
fu_input_stream_read_bytes(FuInputStream *stream,
			   gsize offset,
			   gsize count,
			   FuProgress *progress,
			   GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
gchar *
fu_input_stream_read_string(FuInputStream *stream, gsize offset, gsize count, GError **error)
    G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);

typedef gboolean (*FuInputStreamChunkifyFunc)(const guint8 *buf,
					      gsize bufsz,
					      gpointer user_data,
					      GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_input_stream_chunkify(FuInputStream *stream,
			 FuInputStreamChunkifyFunc func_cb,
			 gpointer user_data,
			 GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 2);

gboolean
fu_input_stream_compute_sum8(FuInputStream *stream,
			     guint8 *value,
			     GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 2);
gboolean
fu_input_stream_compute_sum16(FuInputStream *stream,
			      guint16 *value,
			      GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 2);
gboolean
fu_input_stream_compute_sum32(FuInputStream *stream,
			      guint32 *value,
			      GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 2);
gboolean
fu_input_stream_compute_crc16(FuInputStream *stream, FuCrcKind kind, guint16 *crc, GError **error)
    G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 3);
gboolean
fu_input_stream_compute_crc32(FuInputStream *stream, FuCrcKind kind, guint32 *crc, GError **error)
    G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 3);
gchar *
fu_input_stream_compute_checksum(FuInputStream *stream,
				 GChecksumType checksum_type,
				 GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
gboolean
fu_input_stream_find(FuInputStream *stream,
		     const guint8 *buf,
		     gsize bufsz,
		     gsize offset,
		     gsize *offset_found,
		     GError **error) G_GNUC_NON_NULL(1, 2);
