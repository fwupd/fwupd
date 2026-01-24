/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-test.h"

static void
fu_input_stream_find_func(void)
{
	const gchar *haystack = "I write free software. Firmware troublemaker, writing Firmware.";
	const gchar *needle1 = "Firmware";
	const gchar *needle2 = "XXX";
	gboolean ret;
	gsize offset = 0;
	g_autoptr(GError) error = NULL;
	g_autoptr(GInputStream) stream = NULL;

	stream =
	    g_memory_input_stream_new_from_data((const guint8 *)haystack, strlen(haystack), NULL);
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
	g_autoptr(GInputStream) stream =
	    g_memory_input_stream_new_from_data(buf, sizeof(buf), NULL);

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
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autofree gchar *checksum = NULL;
	g_autofree gchar *checksum2 = NULL;

	for (guint i = 0; i < 0x80000; i++)
		fu_byte_array_append_uint8(buf, i);
	blob = g_bytes_new(buf->data, buf->len);
	stream = g_memory_input_stream_new_from_bytes(blob);

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
	g_autofree guint8 *buf = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GInputStream) stream = NULL;

	fn = g_test_build_filename(G_TEST_DIST, "tests", "dfu.builder.xml", NULL);
	g_assert_nonnull(fn);
	ret = g_file_get_contents(fn, (gchar **)&buf, &bufsz, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	fu_dump_raw(G_LOG_DOMAIN, "src", buf, bufsz);
	csum = g_compute_checksum_for_data(G_CHECKSUM_MD5, (const guchar *)buf, bufsz);

	file = g_file_new_for_path(fn);
	stream = G_INPUT_STREAM(g_file_read(file, NULL, &error));
	g_assert_no_error(error);
	g_assert_nonnull(stream);

	/* verify size */
	ret = fu_input_stream_size(stream, &streamsz, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(streamsz, ==, bufsz);

	/* verify checksum */
	csum2 = fu_input_stream_compute_checksum(stream, G_CHECKSUM_MD5, &error);
	g_assert_no_error(error);
	g_assert_nonnull(csum2);
	g_assert_cmpstr(csum, ==, csum2);

	/* read first byte */
	buf2 = g_malloc0(bufsz);
	ret = fu_input_stream_read_safe(stream, buf2, bufsz, 0x0, 0x0, 1, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(buf[0], ==, buf2[0]);
	fu_dump_raw(G_LOG_DOMAIN, "dst", buf2, bufsz);

	/* read bytes 2,3 */
	ret = fu_input_stream_read_safe(stream,
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
	ret = fu_input_stream_read_safe(stream,
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

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/input-stream", fu_input_stream_func);
	g_test_add_func("/fwupd/input-stream/sum-overflow", fu_input_stream_sum_overflow_func);
	g_test_add_func("/fwupd/input-stream/chunkify", fu_input_stream_chunkify_func);
	g_test_add_func("/fwupd/input-stream/find", fu_input_stream_find_func);
	return g_test_run();
}
