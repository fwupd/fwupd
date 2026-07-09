/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * C header for the Rust compressor FFI functions in the fwupd-ffi crate.
 * This header is hand-written; keep it in sync with
 * rust/fwupd-ffi/src/compressor.rs.
 */

#pragma once

#include <glib.h>

#include "fwupd-rust-streams.h"

#include "fu-compressor-struct.h"

G_BEGIN_DECLS

G_STATIC_ASSERT(sizeof(FuCompressorFormat) == 4);

gint32
fu_rs_compressor_decompress(FuCompressorFormat format,
			    const guint8 *in_buf,
			    gsize in_len,
			    guint8 **out_buf,
			    gsize *out_len,
			    GError **error);
gint32
fu_rs_compressor_compress(FuCompressorFormat format,
			  const guint8 *in_buf,
			  gsize in_len,
			  guint8 **out_buf,
			  gsize *out_len,
			  GError **error);
void
fu_rs_compressor_free(guint8 *ptr, gsize len);

typedef struct FuRsCompressorStream FuRsCompressorStream;

typedef gssize (*FuRsCompressorStreamReadFn)(void *handle, guint8 *buf, gsize count);

FuRsCompressorStream *
fu_rs_compressor_stream_new_decompress(FuRsStreamImpl *base_stream,
				       FuCompressorFormat format,
				       GError **error);
FuRsCompressorStream *
fu_rs_compressor_stream_new_compress(FuRsStreamImpl *base_stream,
				     FuCompressorFormat format,
				     GError **error);
gssize
fu_rs_compressor_stream_read(FuRsCompressorStream *stream, guint8 *buf, gsize count);
void
fu_rs_compressor_stream_free(FuRsCompressorStream *stream);
FuRsStreamImpl *
fu_rs_compressor_stream_get_stream_impl(FuRsCompressorStream *stream);

G_END_DECLS
