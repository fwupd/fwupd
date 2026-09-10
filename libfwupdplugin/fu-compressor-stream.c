/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuCompressorStream"

#include "config.h"

#include "fwupd-rust-compressor-stream.h"

#include "fu-compressor-stream.h"
#include "fu-input-stream.h"

/**
 * FuCompressorStream:
 *
 * An input stream that wraps a zlib compressor or decompressor around a
 * source #FuInputStream. Use fu_compressor_stream_new_compress() to create
 * a stream that compresses data on read, or fu_compressor_stream_new_decompress()
 * to create a stream that decompresses data on read.
 */

struct _FuCompressorStream {
	FuInputStream parent_instance;
	FuInputStream *source;
	gboolean compress;
	union {
		FuRsCompressorStream *compressor;
		FuRsDecompressorStream *decompressor;
	} rust;
};

G_DEFINE_TYPE(FuCompressorStream, fu_compressor_stream, FU_TYPE_INPUT_STREAM)

static gssize
fu_compressor_stream_read_fn(FuInputStream *stream,
			     void *buffer,
			     gsize count,
			     GCancellable *cancellable,
			     GError **error)
{
	FuCompressorStream *self = FU_COMPRESSOR_STREAM(stream);
	gssize rc;

	g_return_val_if_fail(FU_IS_COMPRESSOR_STREAM(self), -1);
	g_return_val_if_fail(error == NULL || *error == NULL, -1);

	if (g_cancellable_set_error_if_cancelled(cancellable, error))
		return -1;

	if (self->compress)
		rc = fu_rs_compressor_stream_read(self->rust.compressor, buffer, count);
	else
		rc = fu_rs_decompressor_stream_read(self->rust.decompressor, buffer, count);
	if (rc < 0) {
		/* nocheck:error not a fwupd error to keep previous GInputStream behavior */
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "compressor stream read failed");
		return -1;
	}
	return rc;
}

static FuRsStreamImpl *
fu_compressor_stream_get_stream_impl(FuInputStream *stream)
{
	FuCompressorStream *self = FU_COMPRESSOR_STREAM(stream);
	FuRsStreamImpl *impl;

	if (self->compress)
		impl = fu_rs_compressor_stream_get_stream_impl(self->rust.compressor);
	else
		impl = fu_rs_decompressor_stream_get_stream_impl(self->rust.decompressor);

	return impl;
}

static FuInputStream *
fu_compressor_stream_new(FuInputStream *source,
			 FwupdCompressorFormat format,
			 gboolean compress,
			 GError **error)
{
	g_autoptr(FuCompressorStream) self = g_object_new(FU_TYPE_COMPRESSOR_STREAM, NULL);
	FuRsStreamImpl *impl;

	g_return_val_if_fail(FU_IS_INPUT_STREAM(source), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	self->source = g_object_ref(source);
	impl = fu_input_stream_get_stream_impl(source);
	if (impl == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "compressor streams require a Rust-backed source stream");
		return NULL;
	}

	self->compress = compress;
	if (compress) {
		self->rust.compressor = fu_rs_compressor_stream_new_compress(impl, format, error);
		if (self->rust.compressor == NULL)
			return NULL;
	} else {
		self->rust.decompressor =
		    fu_rs_compressor_stream_new_decompress(impl, format, error);
		if (self->rust.decompressor == NULL)
			return NULL;
	}

	return FU_INPUT_STREAM(g_steal_pointer(&self));
}

/**
 * fu_compressor_stream_new_decompress:
 * @source: a #FuInputStream with compressed data
 * @format: (type guint): a #FwupdCompressorFormat, e.g. %FWUPD_COMPRESSOR_FORMAT_GZIP
 * @error: (nullable): optional return location for an error
 *
 * Creates a new stream that decompresses data from @source on the fly.
 *
 * Returns: (transfer full): a #FuInputStream, or %NULL on error
 *
 * Since: 2.0.7
 **/
FuInputStream *
fu_compressor_stream_new_decompress(FuInputStream *source,
				    FwupdCompressorFormat format,
				    GError **error)
{
	return fu_compressor_stream_new(source, format, FALSE, error);
}

/**
 * fu_compressor_stream_new_compress:
 * @source: a #FuInputStream with uncompressed data
 * @format: (type guint): a #FwupdCompressorFormat, e.g. %FWUPD_COMPRESSOR_FORMAT_ZLIB
 * @error: (nullable): optional return location for an error
 *
 * Creates a new stream that compresses data from @source on the fly.
 *
 * Returns: (transfer full): a #FuInputStream, or %NULL on error
 *
 * Since: 2.0.7
 **/
FuInputStream *
fu_compressor_stream_new_compress(FuInputStream *source,
				  FwupdCompressorFormat format,
				  GError **error)
{
	return fu_compressor_stream_new(source, format, TRUE, error);
}

static void
fu_compressor_stream_finalize(GObject *object)
{
	FuCompressorStream *self = FU_COMPRESSOR_STREAM(object);
	/* free the Rust stream first */
	if (self->compress) {
		if (self->rust.compressor != NULL)
			fu_rs_compressor_stream_free(self->rust.compressor);
	} else {
		if (self->rust.decompressor != NULL)
			fu_rs_decompressor_stream_free(self->rust.decompressor);
	}
	g_clear_object(&self->source);
	G_OBJECT_CLASS(fu_compressor_stream_parent_class)->finalize(object);
}

static void
fu_compressor_stream_class_init(FuCompressorStreamClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuInputStreamClass *istream_class = FU_INPUT_STREAM_CLASS(klass);
	istream_class->read_fn = fu_compressor_stream_read_fn;
	istream_class->get_stream_impl = fu_compressor_stream_get_stream_impl;
	object_class->finalize = fu_compressor_stream_finalize;
}

static void
fu_compressor_stream_init(FuCompressorStream *self)
{
}
