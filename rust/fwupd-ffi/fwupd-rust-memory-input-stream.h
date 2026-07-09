/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * C header for the Rust memory input stream FFI functions.
 * Keep in sync with rust/fwupd-ffi/src/streams.rs.
 */

#pragma once

#include <glib.h>

#include "fwupd-rust-streams.h"

G_BEGIN_DECLS

typedef struct FuRsMemoryInputStream FuRsMemoryInputStream;

FuRsMemoryInputStream *
fu_rs_memory_input_stream_new_from_data(const guint8 *data, gsize len);
void
fu_rs_memory_input_stream_free(FuRsMemoryInputStream *stream);
gssize
fu_rs_memory_input_stream_read(FuRsMemoryInputStream *stream, guint8 *buf, gsize count);
gboolean
fu_rs_memory_input_stream_seek(FuRsMemoryInputStream *stream, goffset offset, gint32 seek_type);
gboolean
fu_rs_memory_input_stream_can_seek(FuRsMemoryInputStream *stream);
goffset
fu_rs_memory_input_stream_tell(FuRsMemoryInputStream *stream);
gsize
fu_rs_memory_input_stream_size(const FuRsMemoryInputStream *stream);
FuRsStreamImpl *
fu_rs_memory_input_stream_get_stream_impl(const FuRsMemoryInputStream *stream);

G_END_DECLS
