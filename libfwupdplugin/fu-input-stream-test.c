/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fwupd-stream-input-stream-private.h"

static void
fu_input_stream_find_func(void)
{
	const gchar *haystack = "I write free software. Firmware troublemaker, writing Firmware.";
	const gchar *needle1 = "Firmware";
	const gchar *needle2 = "XXX";
	gboolean ret;
	gsize offset = 0;
	g_autoptr(GError) error = NULL;
	g_autoptr(FuInputStream) stream = NULL;

	stream =
	    FWUPD_INPUT_STREAM(fwupd_memory_input_stream_new_from_data((const guint8 *)haystack,
								       strlen(haystack),
								       NULL));
	ret = fu_input_stream_find(stream,
				   (const guint8 *)needle1,
				   strlen(needle1),
				   0x0,
				   &offset,
				   &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(offset, ==, 23);

	/* find second match */
	ret = fu_input_stream_find(stream,
				   (const guint8 *)needle1,
				   strlen(needle1),
				   44,
				   &offset,
				   &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(offset, ==, 54);

	ret = fu_input_stream_find(stream,
				   (const guint8 *)needle2,
				   strlen(needle2),
				   0x0,
				   &offset,
				   &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_false(ret);
}

static void
fu_input_stream_sum_overflow_func(void)
{
	guint8 buf[3] = {0};
	gboolean ret;
	guint32 sum32 = 0;
	g_autoptr(GError) error = NULL;
	g_autoptr(FuInputStream) stream =
	    FWUPD_INPUT_STREAM(fwupd_memory_input_stream_new_from_data(buf, sizeof(buf), NULL));

	ret = fu_input_stream_compute_sum32(stream, &sum32, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_READ);
	g_assert_false(ret);
}

static void
fu_input_stream_chunkify_func(void)
{
	gboolean ret;
	guint8 sum8 = 0;
	guint16 crc16 = 0x0;
	guint32 crc32 = 0xffffffff;
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(FuInputStream) stream = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autofree gchar *checksum = NULL;
	g_autofree gchar *checksum2 = NULL;

	for (guint i = 0; i < 0x80000; i++)
		fu_byte_array_append_uint8(buf, i);
	blob = g_bytes_new(buf->data, buf->len);
	stream = FWUPD_INPUT_STREAM(fwupd_memory_input_stream_new_from_bytes(blob));

	ret = fu_input_stream_compute_sum8(stream, &sum8, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(sum8, ==, fu_sum8_bytes(blob));

	checksum = fu_input_stream_compute_checksum(stream, G_CHECKSUM_SHA1, &error);
	g_assert_no_error(error);
	g_assert_nonnull(checksum);
	checksum2 = g_compute_checksum_for_bytes(G_CHECKSUM_SHA1, blob);
	g_assert_cmpstr(checksum, ==, checksum2);

	ret = fu_input_stream_compute_crc16(stream, FU_CRC_KIND_B16_XMODEM, &crc16, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(crc16, ==, fu_crc16(FU_CRC_KIND_B16_XMODEM, buf->data, buf->len));

	ret = fu_input_stream_compute_crc32(stream, FU_CRC_KIND_B32_MPEG2, &crc32, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(crc32, ==, fu_crc32(FU_CRC_KIND_B32_MPEG2, buf->data, buf->len));

	/* use zlib speedup */
	crc32 = G_MAXUINT32;
	ret = fu_input_stream_compute_crc32(stream, FU_CRC_KIND_B32_STANDARD, &crc32, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(crc32, ==, fu_crc32(FU_CRC_KIND_B32_STANDARD, buf->data, buf->len));
}

static void
fu_input_stream_func(void)
{
	gboolean ret;
	gsize bufsz = 0;
	gsize streamsz = 0;
	g_autofree gchar *csum2 = NULL;
	g_autofree gchar *csum = NULL;
	g_autofree gchar *fn = NULL;
	g_autofree guint8 *buf2 = NULL;
	g_autofree guint8 *buf3 = NULL;
	g_autofree guint8 *buf4 = NULL;
	g_autofree guint8 *buf = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(FwupdFileInputStream) stream = NULL;

	fn = g_test_build_filename(G_TEST_DIST, "tests", "dfu.builder.xml", NULL);
	g_assert_nonnull(fn);
	ret = g_file_get_contents(fn, (gchar **)&buf, &bufsz, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	fu_dump_raw(G_LOG_DOMAIN, "src", buf, bufsz);
	csum = g_compute_checksum_for_data(G_CHECKSUM_MD5, (const guchar *)buf, bufsz);

	stream = fwupd_file_input_stream_new(fn, &error);
	g_assert_no_error(error);
	g_assert_nonnull(stream);

	/* verify size */
	ret = fu_input_stream_size(FWUPD_INPUT_STREAM(stream), &streamsz, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(streamsz, ==, bufsz);

	/* verify we're still at the beginning of the stream - read first half */
	buf3 = g_malloc0(bufsz / 2);
	ret = fu_input_stream_read(FWUPD_INPUT_STREAM(stream), buf3, bufsz / 2, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(ret, ==, bufsz / 2);
	g_assert_cmpmem(buf, bufsz / 2, buf3, bufsz / 2);

	/* verify size again, position-independent */
	ret = fu_input_stream_size(FWUPD_INPUT_STREAM(stream), &streamsz, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(streamsz, ==, bufsz);

	/* size shouldn't have changed our position in the stream - read second half */
	buf4 = g_malloc0(bufsz / 2);
	ret = fu_input_stream_read(FWUPD_INPUT_STREAM(stream), buf4, bufsz / 2, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(ret, ==, bufsz / 2);
	g_assert_cmpmem(buf + (bufsz / 2), bufsz / 2, buf4, bufsz / 2);

	/* verify checksum */
	csum2 =
	    fu_input_stream_compute_checksum(FWUPD_INPUT_STREAM(stream), G_CHECKSUM_MD5, &error);
	g_assert_no_error(error);
	g_assert_nonnull(csum2);
	g_assert_cmpstr(csum, ==, csum2);

	/* read first byte */
	buf2 = g_malloc0(bufsz);
	ret =
	    fu_input_stream_read_safe(FWUPD_INPUT_STREAM(stream), buf2, bufsz, 0x0, 0x0, 1, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(buf[0], ==, buf2[0]);
	fu_dump_raw(G_LOG_DOMAIN, "dst", buf2, bufsz);

	/* read bytes 2,3 */
	ret = fu_input_stream_read_safe(FWUPD_INPUT_STREAM(stream),
					buf2,
					bufsz,
					0x1, /* offset */
					0x1, /* seek */
					2,   /* count */
					&error);
	g_assert_no_error(error);
	g_assert_true(ret);
	fu_dump_raw(G_LOG_DOMAIN, "dst", buf2, bufsz);
	g_assert_cmpint(buf[1], ==, buf2[1]);
	g_assert_cmpint(buf[2], ==, buf2[2]);

	/* read past end of stream */
	ret = fu_input_stream_read_safe(FWUPD_INPUT_STREAM(stream),
					buf2,
					bufsz,
					0x0,   /* offset */
					0x20,  /* seek */
					bufsz, /* count */
					&error);
	fu_dump_raw(G_LOG_DOMAIN, "dst", buf2, bufsz);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_READ);
	g_assert_false(ret);
}

static void
fu_input_stream_read_bytes_func(void)
{
	const guint8 *data;
	gsize sz = 0;
	g_autoptr(GBytes) blob = g_bytes_new_static("ABCDEFGH", 8);
	g_autoptr(GError) error = NULL;
	g_autoptr(GInputStream) base_stream =		/* nocheck:blocked */
	    g_memory_input_stream_new_from_bytes(blob); /* nocheck:blocked */
	g_autoptr(FuInputStream) stream =
	    FWUPD_INPUT_STREAM(fwupd_stream_input_stream_new(base_stream));
	g_autoptr(GBytes) result = NULL;

	result = fu_input_stream_read_bytes(stream, 2, 4, NULL, &error);
	g_assert_no_error(error);
	g_assert_nonnull(result);
	g_assert_cmpint(g_bytes_get_size(result), ==, 4);
	data = g_bytes_get_data(result, &sz);
	g_assert_cmpint(sz, ==, 4);
	g_assert_cmpint(data[0], ==, 'C');
	g_assert_cmpint(data[1], ==, 'D');
	g_assert_cmpint(data[2], ==, 'E');
	g_assert_cmpint(data[3], ==, 'F');
}

static void
fu_input_stream_size_func(void)
{
	gboolean ret;
	gsize sz = 0;
	g_autoptr(GBytes) blob = g_bytes_new_static("ABCDEFGH", 8);
	g_autoptr(GError) error = NULL;
	g_autoptr(GInputStream) base_stream =		/* nocheck:blocked */
	    g_memory_input_stream_new_from_bytes(blob); /* nocheck:blocked */
	g_autoptr(FuInputStream) stream =
	    FWUPD_INPUT_STREAM(fwupd_stream_input_stream_new(base_stream));

	ret = fu_input_stream_size(stream, &sz, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(sz, ==, 8);
}

static void
fu_input_stream_read_safe_func(void)
{
	gboolean ret;
	guint8 buf[8] = {0};
	g_autoptr(GBytes) blob = g_bytes_new_static("ABCDEFGH", 8);
	g_autoptr(GError) error = NULL;
	g_autoptr(GInputStream) base_stream =		/* nocheck:blocked */
	    g_memory_input_stream_new_from_bytes(blob); /* nocheck:blocked */
	g_autoptr(FuInputStream) stream =
	    FWUPD_INPUT_STREAM(fwupd_stream_input_stream_new(base_stream));

	/* read 3 bytes from offset 2 in the stream, store at offset 0 in buf */
	ret = fu_input_stream_read_safe(stream, buf, sizeof(buf), 0x0, 0x2, 3, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(buf[0], ==, 'C');
	g_assert_cmpint(buf[1], ==, 'D');
	g_assert_cmpint(buf[2], ==, 'E');
}

static void
fu_input_stream_compute_checksum_func(void)
{
	g_autoptr(GBytes) blob = g_bytes_new_static("ABCDEFGH", 8);
	g_autoptr(GError) error = NULL;
	g_autoptr(GInputStream) base_stream =		/* nocheck:blocked */
	    g_memory_input_stream_new_from_bytes(blob); /* nocheck:blocked */
	g_autoptr(FuInputStream) stream =
	    FWUPD_INPUT_STREAM(fwupd_stream_input_stream_new(base_stream));
	g_autofree gchar *csum_wrapper = NULL;
	g_autofree gchar *csum_expected = NULL;

	csum_expected =
	    g_compute_checksum_for_data(G_CHECKSUM_SHA256, (const guint8 *)"ABCDEFGH", 8);
	csum_wrapper = fu_input_stream_compute_checksum(stream, G_CHECKSUM_SHA256, &error);
	g_assert_no_error(error);
	g_assert_nonnull(csum_wrapper);
	g_assert_cmpstr(csum_wrapper, ==, csum_expected);
}

static void
fu_input_stream_size_empty_func(void)
{
	gboolean ret;
	gssize rc;
	gsize sz = 0;
	guint8 buf[4] = {0};
	g_autoptr(GBytes) blob = g_bytes_new_static("", 0);
	g_autoptr(GError) error = NULL;
	g_autoptr(GInputStream) base_stream =		/* nocheck:blocked */
	    g_memory_input_stream_new_from_bytes(blob); /* nocheck:blocked */
	g_autoptr(FuInputStream) stream =
	    FWUPD_INPUT_STREAM(fwupd_stream_input_stream_new(base_stream));

	/* size should be 0 */
	ret = fu_input_stream_size(stream, &sz, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(sz, ==, 0);

	/* read should return 0 immediately */
	rc = fu_input_stream_read(stream, buf, sizeof(buf), NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 0);

	/* tell should be at 0 */
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(stream)), ==, 0);
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/input-stream", fu_input_stream_func);
	g_test_add_func("/fwupd/input-stream/sum-overflow", fu_input_stream_sum_overflow_func);
	g_test_add_func("/fwupd/input-stream/chunkify", fu_input_stream_chunkify_func);
	g_test_add_func("/fwupd/input-stream/find", fu_input_stream_find_func);
	g_test_add_func("/fwupd/input-stream/read-bytes", fu_input_stream_read_bytes_func);
	g_test_add_func("/fwupd/input-stream/size", fu_input_stream_size_func);
	g_test_add_func("/fwupd/input-stream/read-safe", fu_input_stream_read_safe_func);
	g_test_add_func("/fwupd/input-stream/compute-checksum",
			fu_input_stream_compute_checksum_func);
	g_test_add_func("/fwupd/input-stream/size-empty", fu_input_stream_size_empty_func);
	return g_test_run();
}
