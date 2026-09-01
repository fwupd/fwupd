/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * C header for the Rust CStream FFI functions.
 * Keep in sync with rust/fwupd-ffi/src/cstream.rs and rust/fwupd-ffi/src/streams.rs.
 */

#pragma once

#include <glib.h>

#include "fwupd-rust-streams.h"

G_BEGIN_DECLS

typedef struct CStream FuRsCStream;
typedef void *FuRsExternalStreamHandle;

/*
 * Callback vtable for reading/seeking on a C-side stream.
 *
 * Used by FuRsPartialInputStream and FuRsCompositeInputStream to
 * delegate I/O to the underlying C GInputStream objects.
 *
 * seek_fn returns gboolean (TRUE on success, FALSE on error).
 */
typedef struct {
	gssize (*read_fn)(FuRsExternalStreamHandle handle,
			  guint8 *buf,
			  gsize count,
			  GError **error);
	gboolean (*seek_fn)(FuRsExternalStreamHandle handle,
			    goffset offset,
			    gint32 seek_type,
			    GError **error);
	gboolean (*can_seek_fn)(FuRsExternalStreamHandle handle);
	goffset (*tell_fn)(FuRsExternalStreamHandle handle);
	void (*destroy_fn)(FuRsExternalStreamHandle handle);
} FuRsStreamCallbacks;

FuRsCStream *
fu_rs_cstream_new(FuRsExternalStreamHandle handle, FuRsStreamCallbacks callbacks);
void
fu_rs_cstream_free(FuRsCStream *stream);
gssize
fu_rs_cstream_read(FuRsCStream *stream, guint8 *buf, gsize count, GError **error);
gboolean
fu_rs_cstream_seek(FuRsCStream *stream, goffset offset, gint32 seek_type, GError **error);
gboolean
fu_rs_cstream_can_seek(FuRsCStream *stream);
goffset
fu_rs_cstream_tell(FuRsCStream *stream);
gsize
fu_rs_cstream_size(FuRsCStream *stream);
FuRsStreamImpl *
fu_rs_cstream_get_stream_impl(const FuRsCStream *stream);

G_END_DECLS
