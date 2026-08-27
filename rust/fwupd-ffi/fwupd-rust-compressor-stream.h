/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * C header for the Rust compressor stream FFI functions in the fwupd-ffi crate.
 * This header is hand-written; keep it in sync with
 * rust/fwupd-ffi/src/streams.rs.
 */

#pragma once

#include <glib.h>

#include "fwupd-compressor-private.h"
#include "fwupd-rust-streams.h"

G_BEGIN_DECLS

typedef struct FuRsCompressorStream FuRsCompressorStream;
typedef struct FuRsDecompressorStream FuRsDecompressorStream;

FuRsCompressorStream *
fu_rs_compressor_stream_new_compress(FuRsStreamImpl *base_stream,
				     FwupdCompressorFormat format,
				     GError **error);
gssize
fu_rs_compressor_stream_read(FuRsCompressorStream *stream, guint8 *buf, gsize count);
gboolean
fu_rs_compressor_stream_can_seek(FuRsCompressorStream *stream);
void
fu_rs_compressor_stream_free(FuRsCompressorStream *stream);
FuRsStreamImpl *
fu_rs_compressor_stream_get_stream_impl(const FuRsCompressorStream *stream);

FuRsDecompressorStream *
fu_rs_compressor_stream_new_decompress(FuRsStreamImpl *base_stream,
				       FwupdCompressorFormat format,
				       GError **error);
gssize
fu_rs_decompressor_stream_read(FuRsDecompressorStream *stream, guint8 *buf, gsize count);
gboolean
fu_rs_decompressor_stream_can_seek(FuRsDecompressorStream *stream);
void
fu_rs_decompressor_stream_free(FuRsDecompressorStream *stream);
FuRsStreamImpl *
fu_rs_decompressor_stream_get_stream_impl(const FuRsDecompressorStream *stream);

G_END_DECLS
