/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuCompressorStream"

#include "config.h"

#include "fu-compressor-stream.h"
#include "fu-stream-input-stream-private.h"

/**
 * FuCompressorStream:
 *
 * An input stream that wraps a zlib compressor or decompressor around a
 * source #FuInputStream. Use fu_compressor_stream_new_compress() to create
 * a stream that compresses data on read, or fu_compressor_stream_new_decompress()
 * to create a stream that decompresses data on read.
 */

struct _FuCompressorStream {
	FuStreamInputStream parent_instance;
	GConverter *conv;
	GInputStream *converter_stream; /* nocheck:blocked */
};

G_DEFINE_TYPE(FuCompressorStream, fu_compressor_stream, FU_TYPE_STREAM_INPUT_STREAM)

static GZlibCompressorFormat
fu_compressor_stream_format_to_glib(FuCompressorFormat format)
{
	if (format == FU_COMPRESSOR_FORMAT_RAW)
		return G_ZLIB_COMPRESSOR_FORMAT_RAW;
	if (format == FU_COMPRESSOR_FORMAT_GZIP)
		return G_ZLIB_COMPRESSOR_FORMAT_GZIP;
	return G_ZLIB_COMPRESSOR_FORMAT_ZLIB;
}

static void
fu_compressor_stream_finalize(GObject *object)
{
	FuCompressorStream *self = FU_COMPRESSOR_STREAM(object);
	g_clear_object(&self->converter_stream);
	g_clear_object(&self->conv);
	G_OBJECT_CLASS(fu_compressor_stream_parent_class)->finalize(object);
}

static void
fu_compressor_stream_class_init(FuCompressorStreamClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_compressor_stream_finalize;
}

static void
fu_compressor_stream_init(FuCompressorStream *self)
{
}

/**
 * fu_compressor_stream_new_decompress:
 * @source: a #FuInputStream with compressed data
 * @format: a #FuCompressorFormat, e.g. %FU_COMPRESSOR_FORMAT_GZIP
 * @error: (nullable): optional return location for an error
 *
 * Creates a new stream that decompresses data from @source on read.
 *
 * Returns: (transfer full): a #FuInputStream, or %NULL on error
 *
 * Since: 2.0.7
 **/
FuInputStream *
fu_compressor_stream_new_decompress(FuInputStream *source,
				    FuCompressorFormat format,
				    GError **error)
{
	g_autoptr(FuCompressorStream) self = NULL;

	g_return_val_if_fail(FU_IS_INPUT_STREAM(source), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	self = g_object_new(FU_TYPE_COMPRESSOR_STREAM, NULL);

	self->conv = G_CONVERTER(g_zlib_decompressor_new(
	    fu_compressor_stream_format_to_glib(format))); /* nocheck:blocked */
	self->converter_stream = g_converter_input_stream_new(G_INPUT_STREAM(source), self->conv);
	g_filter_input_stream_set_close_base_stream(G_FILTER_INPUT_STREAM(self->converter_stream),
						    FALSE);
	fu_stream_input_stream_set_base_stream(FU_STREAM_INPUT_STREAM(self),
					       self->converter_stream);

	return FU_INPUT_STREAM(g_steal_pointer(&self));
}

/**
 * fu_compressor_stream_new_compress:
 * @source: a #FuInputStream with uncompressed data
 * @format: a #FuCompressorFormat, e.g. %FU_COMPRESSOR_FORMAT_ZLIB
 * @error: (nullable): optional return location for an error
 *
 * Creates a new stream that compresses data from @source on read.
 *
 * Returns: (transfer full): a #FuInputStream, or %NULL on error
 *
 * Since: 2.0.7
 **/
FuInputStream *
fu_compressor_stream_new_compress(FuInputStream *source, FuCompressorFormat format, GError **error)
{
	g_autoptr(FuCompressorStream) self = NULL;

	g_return_val_if_fail(FU_IS_INPUT_STREAM(source), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	self = g_object_new(FU_TYPE_COMPRESSOR_STREAM, NULL);

	self->conv = G_CONVERTER(g_zlib_compressor_new(fu_compressor_stream_format_to_glib(format),
						       -1)); /* nocheck:blocked */
	self->converter_stream = g_converter_input_stream_new(G_INPUT_STREAM(source), self->conv);
	g_filter_input_stream_set_close_base_stream(G_FILTER_INPUT_STREAM(self->converter_stream),
						    FALSE);
	fu_stream_input_stream_set_base_stream(FU_STREAM_INPUT_STREAM(self),
					       self->converter_stream);

	return FU_INPUT_STREAM(g_steal_pointer(&self));
}
