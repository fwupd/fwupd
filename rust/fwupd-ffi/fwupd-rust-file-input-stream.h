/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * C header for the Rust file input stream FFI functions.
 * Keep in sync with rust/fwupd-ffi/src/streams.rs.
 */

#pragma once

#include <glib.h>

#include "fwupd-rust-streams.h"

G_BEGIN_DECLS

typedef struct FuRsFileInputStream FuRsFileInputStream;

FuRsFileInputStream *
fu_rs_file_input_stream_new_from_path(const gchar *path, GError **error);
FuRsFileInputStream *
fu_rs_file_input_stream_new_from_fd(int fd, GError **error);
int
fu_rs_file_input_stream_get_fd(FuRsFileInputStream *stream);
void
fu_rs_file_input_stream_free(FuRsFileInputStream *stream);
gssize
fu_rs_file_input_stream_read(FuRsFileInputStream *stream, guint8 *buf, gsize count);
gboolean
fu_rs_file_input_stream_seek(FuRsFileInputStream *stream, goffset offset, gint32 seek_type);
gboolean
fu_rs_file_input_stream_can_seek(FuRsFileInputStream *stream);
goffset
fu_rs_file_input_stream_tell(FuRsFileInputStream *stream);
gsize
fu_rs_file_input_stream_size(const FuRsFileInputStream *stream);
FuRsStreamImpl *
fu_rs_file_input_stream_get_stream_impl(const FuRsFileInputStream *stream);

G_END_DECLS
