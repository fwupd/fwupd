/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * C header for the Rust composite input stream FFI functions.
 * Keep in sync with rust/fwupd-ffi/src/streams.rs.
 */

#pragma once

#include <glib.h>

#include "fwupd-rust-streams.h"

G_BEGIN_DECLS

typedef struct FuRsCompositeInputStream FuRsCompositeInputStream;

FuRsCompositeInputStream *
fu_rs_composite_input_stream_new(void);
void
fu_rs_composite_input_stream_free(FuRsCompositeInputStream *stream);
void
fu_rs_composite_input_stream_add_stream(FuRsCompositeInputStream *stream,
					FuRsStreamImpl *sub_stream,
					gsize size);
gssize
fu_rs_composite_input_stream_read(FuRsCompositeInputStream *stream, guint8 *buf, gsize count);
gboolean
fu_rs_composite_input_stream_seek(FuRsCompositeInputStream *stream,
				  goffset offset,
				  gint32 seek_type);
gboolean
fu_rs_composite_input_stream_can_seek(FuRsCompositeInputStream *stream);
goffset
fu_rs_composite_input_stream_tell(FuRsCompositeInputStream *stream);
gsize
fu_rs_composite_input_stream_size(const FuRsCompositeInputStream *stream);
FuRsStreamImpl *
fu_rs_composite_input_stream_get_stream_impl(const FuRsCompositeInputStream *stream);

G_END_DECLS
