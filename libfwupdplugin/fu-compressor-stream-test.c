/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

static GBytes *
fu_test_create_pattern(gsize sz)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();
	for (gsize i = 0; i < sz; i++) {
		guint8 tmp = i % 251;
		g_byte_array_append(buf, &tmp, 1);
	}
	return g_bytes_new(buf->data, buf->len);
}

static GBytes *
fu_test_compress(GBytes *blob, FuCompressorFormat format, GError **error)
{
	g_autoptr(FuInputStream) source = fu_memory_input_stream_new_from_bytes(blob);
	g_autoptr(FuInputStream) cstream = fu_compressor_stream_new_compress(source, format, error);
	if (cstream == NULL)
		return NULL;
	return fu_input_stream_read_bytes(cstream, 0, G_MAXUINT32, NULL, error);
}

static GBytes *
fu_test_decompress(GBytes *blob, FuCompressorFormat format, GError **error)
{
	g_autoptr(FuInputStream) source = fu_memory_input_stream_new_from_bytes(blob);
	g_autoptr(FuInputStream) dstream =
	    fu_compressor_stream_new_decompress(source, format, error);
	if (dstream == NULL)
		return NULL;
	return fu_input_stream_read_bytes(dstream, 0, G_MAXUINT32, NULL, error);
}

static GBytes *
fu_test_decompress_gconverter(GBytes *blob, FuCompressorFormat format, GError **error)
{
	GZlibCompressorFormat gformat;
	gssize rc;
	g_autoptr(GConverter) conv = NULL;
	g_autoptr(GInputStream) base = NULL;		 /* nocheck:blocked */
	g_autoptr(GInputStream) converter_stream = NULL; /* nocheck:blocked */
	g_autoptr(GByteArray) buf = g_byte_array_new();
	guint8 tmp[4096] = {0}; /* nocheck:zero-init */

	if (format == FU_COMPRESSOR_FORMAT_RAW)
		gformat = G_ZLIB_COMPRESSOR_FORMAT_RAW;
	else if (format == FU_COMPRESSOR_FORMAT_GZIP)
		gformat = G_ZLIB_COMPRESSOR_FORMAT_GZIP;
	else
		gformat = G_ZLIB_COMPRESSOR_FORMAT_ZLIB;

	conv = G_CONVERTER(g_zlib_decompressor_new(gformat)); /* nocheck:blocked */
	base = g_memory_input_stream_new_from_bytes(blob);    /* nocheck:blocked */
	converter_stream = g_converter_input_stream_new(base, conv);

	while (TRUE) {
		rc = g_input_stream_read(converter_stream, /* nocheck:blocked */
					 tmp,
					 sizeof(tmp),
					 NULL,
					 error);
		if (rc < 0)
			return NULL;
		if (rc == 0)
			break;
		g_byte_array_append(buf, tmp, rc);
	}

	return g_bytes_new(buf->data, buf->len);
}

static void
fu_compressor_stream_roundtrip_zlib_func(void)
{
	gboolean ret;
	g_autoptr(GBytes) original = fu_test_create_pattern(10000);
	g_autoptr(GBytes) compressed = NULL;
	g_autoptr(GBytes) decompressed = NULL;
	g_autoptr(GBytes) decompressed_gconv = NULL;
	g_autoptr(GError) error = NULL;

	compressed = fu_test_compress(original, FU_COMPRESSOR_FORMAT_ZLIB, &error);
	g_assert_no_error(error);
	g_assert_nonnull(compressed);

	g_assert_cmpint(g_bytes_get_size(compressed), <, g_bytes_get_size(original));

	decompressed = fu_test_decompress(compressed, FU_COMPRESSOR_FORMAT_ZLIB, &error);
	g_assert_no_error(error);
	g_assert_nonnull(decompressed);
	ret = fu_bytes_compare(original, decompressed, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* verify we've actually compressed properly */
	decompressed_gconv =
	    fu_test_decompress_gconverter(compressed, FU_COMPRESSOR_FORMAT_ZLIB, &error);
	g_assert_no_error(error);
	g_assert_nonnull(decompressed_gconv);
	ret = fu_bytes_compare(original, decompressed_gconv, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_compressor_stream_roundtrip_gzip_func(void)
{
	gboolean ret;
	g_autoptr(GBytes) original = fu_test_create_pattern(10000);
	g_autoptr(GBytes) compressed = NULL;
	g_autoptr(GBytes) decompressed = NULL;
	g_autoptr(GBytes) decompressed_gconv = NULL;
	g_autoptr(GError) error = NULL;

	compressed = fu_test_compress(original, FU_COMPRESSOR_FORMAT_GZIP, &error);
	g_assert_no_error(error);
	g_assert_nonnull(compressed);
	g_assert_cmpint(g_bytes_get_size(compressed), <, g_bytes_get_size(original));

	decompressed = fu_test_decompress(compressed, FU_COMPRESSOR_FORMAT_GZIP, &error);
	g_assert_no_error(error);
	g_assert_nonnull(decompressed);
	ret = fu_bytes_compare(original, decompressed, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* verify we've actually compressed properly */
	decompressed_gconv =
	    fu_test_decompress_gconverter(compressed, FU_COMPRESSOR_FORMAT_GZIP, &error);
	g_assert_no_error(error);
	g_assert_nonnull(decompressed_gconv);
	ret = fu_bytes_compare(original, decompressed_gconv, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_compressor_stream_roundtrip_raw_func(void)
{
	gboolean ret;
	g_autoptr(GBytes) original = fu_test_create_pattern(10000);
	g_autoptr(GBytes) compressed = NULL;
	g_autoptr(GBytes) decompressed = NULL;
	g_autoptr(GBytes) decompressed_gconv = NULL;
	g_autoptr(GError) error = NULL;

	compressed = fu_test_compress(original, FU_COMPRESSOR_FORMAT_RAW, &error);
	g_assert_no_error(error);
	g_assert_nonnull(compressed);
	g_assert_cmpint(g_bytes_get_size(compressed), <, g_bytes_get_size(original));

	decompressed = fu_test_decompress(compressed, FU_COMPRESSOR_FORMAT_RAW, &error);
	g_assert_no_error(error);
	g_assert_nonnull(decompressed);
	ret = fu_bytes_compare(original, decompressed, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* verify we've actually compressed properly */
	decompressed_gconv =
	    fu_test_decompress_gconverter(compressed, FU_COMPRESSOR_FORMAT_RAW, &error);
	g_assert_no_error(error);
	g_assert_nonnull(decompressed_gconv);
	ret = fu_bytes_compare(original, decompressed_gconv, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_compressor_stream_type_func(void)
{
	g_autoptr(GBytes) blob = g_bytes_new_static("ABCD", 4);
	g_autoptr(GError) error = NULL;
	g_autoptr(FuInputStream) source = fu_memory_input_stream_new_from_bytes(blob);
	g_autoptr(FuInputStream) stream =
	    fu_compressor_stream_new_compress(source, FU_COMPRESSOR_FORMAT_ZLIB, &error);

	g_assert_no_error(error);
	g_assert_nonnull(stream);
	g_assert_false(G_IS_INPUT_STREAM(stream)); /* nocheck:blocked */
	g_assert_true(G_IS_SEEKABLE(stream));
	g_assert_true(FU_IS_INPUT_STREAM(stream));
	g_assert_true(FU_IS_STREAM_INPUT_STREAM(stream));
	g_assert_true(FU_IS_COMPRESSOR_STREAM(stream));
}

static void
fu_compressor_stream_read_func(void)
{
	gssize rc;
	guint8 buf[1] = {0};
	g_autoptr(GBytes) original = g_bytes_new_static("ABCDEFGH", 8);
	g_autoptr(GBytes) compressed = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(FuInputStream) source = NULL;
	g_autoptr(FuInputStream) dstream = NULL;
	const guint8 *orig_data;
	gsize orig_sz;

	compressed = fu_test_compress(original, FU_COMPRESSOR_FORMAT_ZLIB, &error);
	g_assert_no_error(error);
	g_assert_nonnull(compressed);

	source = fu_memory_input_stream_new_from_bytes(compressed);
	dstream = fu_compressor_stream_new_decompress(source, FU_COMPRESSOR_FORMAT_ZLIB, &error);
	g_assert_no_error(error);
	g_assert_nonnull(dstream);

	orig_data = g_bytes_get_data(original, &orig_sz);
	for (gsize i = 0; i < orig_sz; i++) {
		rc = fu_input_stream_read(dstream, buf, 1, NULL, &error);
		g_assert_no_error(error);
		g_assert_cmpint(rc, ==, 1);
		g_assert_cmpint(buf[0], ==, orig_data[i]);
	}

	/* EOF */
	rc = fu_input_stream_read(dstream, buf, 1, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 0);
}

static void
fu_compressor_stream_decompress_invalid_func(void)
{
	gssize rc;
	guint8 buf[64] = {0};
	g_autoptr(GBytes) garbage = g_bytes_new_static("this is not compressed data!!", 28);
	g_autoptr(GError) error = NULL;
	g_autoptr(FuInputStream) source = fu_memory_input_stream_new_from_bytes(garbage);
	g_autoptr(FuInputStream) dstream =
	    fu_compressor_stream_new_decompress(source, FU_COMPRESSOR_FORMAT_ZLIB, &error);

	/* construction succeeds -- the error happens on read */
	g_assert_no_error(error);
	g_assert_nonnull(dstream);

	rc = fu_input_stream_read(dstream, buf, sizeof(buf), NULL, &error);
	g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
	g_assert_cmpint(rc, ==, -1);
}

static void
fu_compressor_stream_read_bytes_func(void)
{
	gboolean ret;
	g_autoptr(GBytes) original = fu_test_create_pattern(1024);
	g_autoptr(GBytes) compressed = NULL;
	g_autoptr(GBytes) result = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(FuInputStream) source = NULL;
	g_autoptr(FuInputStream) dstream = NULL;

	compressed = fu_test_compress(original, FU_COMPRESSOR_FORMAT_GZIP, &error);
	g_assert_no_error(error);
	g_assert_nonnull(compressed);

	source = fu_memory_input_stream_new_from_bytes(compressed);
	dstream = fu_compressor_stream_new_decompress(source, FU_COMPRESSOR_FORMAT_GZIP, &error);
	g_assert_no_error(error);
	g_assert_nonnull(dstream);

	result = fu_input_stream_read_bytes(dstream, 0, G_MAXUINT32, NULL, &error);
	g_assert_no_error(error);
	g_assert_nonnull(result);

	ret = fu_bytes_compare(original, result, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_compressor_stream_truncate_func(void)
{
	gboolean ret;
	g_autoptr(GBytes) blob = g_bytes_new_static("ABCD", 4);
	g_autoptr(GError) error = NULL;
	g_autoptr(FuInputStream) source = fu_memory_input_stream_new_from_bytes(blob);
	g_autoptr(FuInputStream) stream =
	    fu_compressor_stream_new_compress(source, FU_COMPRESSOR_FORMAT_ZLIB, &error);

	g_assert_no_error(error);
	g_assert_nonnull(stream);
	g_assert_false(g_seekable_can_truncate(G_SEEKABLE(stream)));

	ret = g_seekable_truncate(G_SEEKABLE(stream), 2, NULL, &error);
	g_assert_false(ret);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
}

static void
fu_compressor_stream_empty_func(void)
{
	gssize rc;
	guint8 buf[16] = {0};
	g_autoptr(GBytes) empty = g_bytes_new_static("", 0);
	g_autoptr(GBytes) compressed = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(FuInputStream) source = NULL;
	g_autoptr(FuInputStream) dstream = NULL;

	compressed = fu_test_compress(empty, FU_COMPRESSOR_FORMAT_ZLIB, &error);
	g_assert_no_error(error);
	g_assert_nonnull(compressed);

	/* compressed empty stream should still have some header bytes */
	g_assert_cmpint(g_bytes_get_size(compressed), >, 0);

	/* decompressing should yield zero bytes */
	source = fu_memory_input_stream_new_from_bytes(compressed);
	dstream = fu_compressor_stream_new_decompress(source, FU_COMPRESSOR_FORMAT_ZLIB, &error);
	g_assert_no_error(error);
	g_assert_nonnull(dstream);

	rc = fu_input_stream_read(dstream, buf, sizeof(buf), NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 0);
}

static void
fu_compressor_stream_format_mismatch_func(void)
{
	gssize rc;
	guint8 buf[64] = {0};
	g_autoptr(GBytes) original = fu_test_create_pattern(1000);
	g_autoptr(GBytes) compressed = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(FuInputStream) source = NULL;
	g_autoptr(FuInputStream) dstream = NULL;

	compressed = fu_test_compress(original, FU_COMPRESSOR_FORMAT_GZIP, &error);
	g_assert_no_error(error);
	g_assert_nonnull(compressed);

	/* compressed as GZIP, decompress as ZLIB -- expected to fail */
	source = fu_memory_input_stream_new_from_bytes(compressed);
	dstream = fu_compressor_stream_new_decompress(source, FU_COMPRESSOR_FORMAT_ZLIB, &error);
	g_assert_no_error(error);
	g_assert_nonnull(dstream);

	rc = fu_input_stream_read(dstream, buf, sizeof(buf), NULL, &error);
	g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
	g_assert_cmpint(rc, ==, -1);
}

static void
fu_compressor_stream_large_func(void)
{
	gboolean ret;
	g_autoptr(GBytes) original = fu_test_create_pattern(256 * 1024);
	g_autoptr(GBytes) compressed = NULL;
	g_autoptr(GBytes) decompressed = NULL;
	g_autoptr(GError) error = NULL;

	compressed = fu_test_compress(original, FU_COMPRESSOR_FORMAT_GZIP, &error);
	g_assert_no_error(error);
	g_assert_nonnull(compressed);
	g_assert_cmpint(g_bytes_get_size(compressed), <, g_bytes_get_size(original));

	decompressed = fu_test_decompress(compressed, FU_COMPRESSOR_FORMAT_GZIP, &error);
	g_assert_no_error(error);
	g_assert_nonnull(decompressed);

	ret = fu_bytes_compare(original, decompressed, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_compressor_stream_no_close_source_func(void)
{
	gssize rc;
	guint8 buf[4] = {0};
	g_autoptr(GBytes) blob = g_bytes_new_static("ABCDEFGH", 8);
	g_autoptr(GError) error = NULL;
	g_autoptr(FuInputStream) source = fu_memory_input_stream_new_from_bytes(blob);

	/* create and destroy a compressor stream */
	{
		g_autoptr(FuInputStream) cstream =
		    fu_compressor_stream_new_compress(source, FU_COMPRESSOR_FORMAT_ZLIB, &error);
		g_assert_no_error(error);
		g_assert_nonnull(cstream);
	}

	/* the source should still be readable after the compressor is finalized */
	g_seekable_seek(G_SEEKABLE(source), 0, G_SEEK_SET, NULL, NULL);
	rc = fu_input_stream_read(source, buf, 4, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 4);
	g_assert_cmpint(buf[0], ==, 'A');
	g_assert_cmpint(buf[1], ==, 'B');
	g_assert_cmpint(buf[2], ==, 'C');
	g_assert_cmpint(buf[3], ==, 'D');
}

static void
fu_compressor_stream_checksum_func(void)
{
	gboolean ret;
	g_autoptr(GBytes) original = fu_test_create_pattern(4096);
	g_autoptr(GBytes) compressed = NULL;
	g_autoptr(GBytes) decompressed = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *csum_original = NULL;
	g_autofree gchar *csum_decompressed = NULL;

	compressed = fu_test_compress(original, FU_COMPRESSOR_FORMAT_ZLIB, &error);
	g_assert_no_error(error);
	g_assert_nonnull(compressed);

	decompressed = fu_test_decompress(compressed, FU_COMPRESSOR_FORMAT_ZLIB, &error);
	g_assert_no_error(error);
	g_assert_nonnull(decompressed);

	/* compute checksums on the raw bytes and compare */
	csum_original = g_compute_checksum_for_bytes(G_CHECKSUM_SHA256, original);
	csum_decompressed = g_compute_checksum_for_bytes(G_CHECKSUM_SHA256, decompressed);
	g_assert_cmpstr(csum_original, ==, csum_decompressed);

	ret = fu_bytes_compare(original, decompressed, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/compressor-stream/type", fu_compressor_stream_type_func);
	g_test_add_func("/fwupd/compressor-stream/read", fu_compressor_stream_read_func);
	g_test_add_func("/fwupd/compressor-stream/roundtrip-zlib",
			fu_compressor_stream_roundtrip_zlib_func);
	g_test_add_func("/fwupd/compressor-stream/roundtrip-gzip",
			fu_compressor_stream_roundtrip_gzip_func);
	g_test_add_func("/fwupd/compressor-stream/roundtrip-raw",
			fu_compressor_stream_roundtrip_raw_func);
	g_test_add_func("/fwupd/compressor-stream/decompress-invalid",
			fu_compressor_stream_decompress_invalid_func);
	g_test_add_func("/fwupd/compressor-stream/read-bytes",
			fu_compressor_stream_read_bytes_func);
	g_test_add_func("/fwupd/compressor-stream/truncate", fu_compressor_stream_truncate_func);
	g_test_add_func("/fwupd/compressor-stream/empty", fu_compressor_stream_empty_func);
	g_test_add_func("/fwupd/compressor-stream/format-mismatch",
			fu_compressor_stream_format_mismatch_func);
	g_test_add_func("/fwupd/compressor-stream/large", fu_compressor_stream_large_func);
	g_test_add_func("/fwupd/compressor-stream/no-close-source",
			fu_compressor_stream_no_close_source_func);
	g_test_add_func("/fwupd/compressor-stream/checksum", fu_compressor_stream_checksum_func);
	return g_test_run();
}
