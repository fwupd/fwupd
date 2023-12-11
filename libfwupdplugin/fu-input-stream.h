/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupd.h>

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
gchar *
fu_input_stream_compute_checksum(GInputStream *stream,
				 GChecksumType checksum_type,
				 GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
