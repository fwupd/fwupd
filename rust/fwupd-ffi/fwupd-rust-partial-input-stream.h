/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * C header for the Rust partial input stream FFI functions.
 * Keep in sync with rust/fwupd-ffi/src/streams.rs.
 */

#pragma once

#include <glib.h>

#include "fwupd-rust-streams.h"

G_BEGIN_DECLS

typedef struct FuRsPartialInputStream FuRsPartialInputStream;

FuRsPartialInputStream *
fu_rs_partial_input_stream_new(FuRsStreamImpl *base_stream, gsize offset, gsize size);
void
fu_rs_partial_input_stream_free(FuRsPartialInputStream *stream);
gssize
fu_rs_partial_input_stream_read(FuRsPartialInputStream *stream, guint8 *buf, gsize count);
gboolean
fu_rs_partial_input_stream_seek(FuRsPartialInputStream *stream, goffset offset, gint32 seek_type);
gboolean
fu_rs_partial_input_stream_can_seek(FuRsPartialInputStream *stream);
goffset
fu_rs_partial_input_stream_tell(FuRsPartialInputStream *stream);
gsize
fu_rs_partial_input_stream_size(const FuRsPartialInputStream *stream);
gsize
fu_rs_partial_input_stream_offset(const FuRsPartialInputStream *stream);
FuRsStreamImpl *
fu_rs_partial_input_stream_get_stream_impl(const FuRsPartialInputStream *stream);

G_END_DECLS
