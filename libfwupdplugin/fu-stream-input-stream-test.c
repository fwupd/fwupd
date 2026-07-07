/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-stream-input-stream-private.h"

static void
fu_stream_input_stream_read_func(void)
{
	gssize rc;
	guint8 buf[8] = {0};
	g_autoptr(GBytes) blob = g_bytes_new_static("ABCDEFGH", 8);
	g_autoptr(GError) error = NULL;
	g_autoptr(FuInputStream) base_stream = fu_memory_input_stream_new_from_bytes(blob);
	g_autoptr(FuInputStream) stream =
	    fu_stream_input_stream_from_stream(G_INPUT_STREAM(base_stream));

	/* read first 4 bytes */
	rc = g_input_stream_read(G_INPUT_STREAM(stream), buf, 4, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 4);
	g_assert_cmpint(buf[0], ==, 'A');
	g_assert_cmpint(buf[1], ==, 'B');
	g_assert_cmpint(buf[2], ==, 'C');
	g_assert_cmpint(buf[3], ==, 'D');

	/* read next 4 bytes */
	rc = g_input_stream_read(G_INPUT_STREAM(stream), buf, 4, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 4);
	g_assert_cmpint(buf[0], ==, 'E');
	g_assert_cmpint(buf[1], ==, 'F');
	g_assert_cmpint(buf[2], ==, 'G');
	g_assert_cmpint(buf[3], ==, 'H');

	/* reading past end returns 0 */
	rc = g_input_stream_read(G_INPUT_STREAM(stream), buf, 4, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 0);
}

static void
fu_stream_input_stream_read_short_func(void)
{
	gssize rc;
	guint8 buf[16] = {0};
	g_autoptr(GBytes) blob = g_bytes_new_static("ABC", 3);
	g_autoptr(FuInputStream) base_stream = fu_memory_input_stream_new_from_bytes(blob);
	g_autoptr(FuInputStream) stream =
	    fu_stream_input_stream_from_stream(G_INPUT_STREAM(base_stream));
	g_autoptr(GError) error = NULL;

	rc = g_input_stream_read(G_INPUT_STREAM(stream), buf, sizeof(buf), NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 3);
	g_assert_cmpint(buf[0], ==, 'A');
	g_assert_cmpint(buf[1], ==, 'B');
	g_assert_cmpint(buf[2], ==, 'C');
}

static void
fu_stream_input_stream_seek_func(void)
{
	gboolean ret;
	gssize rc;
	guint8 buf[2] = {0};
	g_autoptr(GBytes) blob = g_bytes_new_static("ABCDEFGH", 8);
	g_autoptr(GError) error = NULL;
	g_autoptr(FuInputStream) base_stream = fu_memory_input_stream_new_from_bytes(blob);
	g_autoptr(FuInputStream) stream =
	    fu_stream_input_stream_from_stream(G_INPUT_STREAM(base_stream));

	/* can_seek should return TRUE for a seekable base stream */
	g_assert_true(g_seekable_can_seek(G_SEEKABLE(stream)));

	/* SEEK_SET to offset 4 */
	ret = g_seekable_seek(G_SEEKABLE(stream), 4, G_SEEK_SET, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(stream)), ==, 4);

	rc = g_input_stream_read(G_INPUT_STREAM(stream), buf, 2, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 2);
	g_assert_cmpint(buf[0], ==, 'E');
	g_assert_cmpint(buf[1], ==, 'F');

	/* SEEK_CUR forward by 1 (now at position 7) */
	ret = g_seekable_seek(G_SEEKABLE(stream), 1, G_SEEK_CUR, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(stream)), ==, 7);

	rc = g_input_stream_read(G_INPUT_STREAM(stream), buf, 1, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 1);
	g_assert_cmpint(buf[0], ==, 'H');

	/* SEEK_END with negative offset */
	ret = g_seekable_seek(G_SEEKABLE(stream), -2, G_SEEK_END, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(stream)), ==, 6);

	rc = g_input_stream_read(G_INPUT_STREAM(stream), buf, 2, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 2);
	g_assert_cmpint(buf[0], ==, 'G');
	g_assert_cmpint(buf[1], ==, 'H');

	/* seek back to start */
	ret = g_seekable_seek(G_SEEKABLE(stream), 0, G_SEEK_SET, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(stream)), ==, 0);
}

static void
fu_stream_input_stream_tell_func(void)
{
	gboolean ret;
	gssize rc;
	guint8 buf[3] = {0};
	g_autoptr(GBytes) blob = g_bytes_new_static("ABCDEF", 6);
	g_autoptr(GError) error = NULL;
	g_autoptr(FuInputStream) base_stream = fu_memory_input_stream_new_from_bytes(blob);
	g_autoptr(FuInputStream) stream =
	    fu_stream_input_stream_from_stream(G_INPUT_STREAM(base_stream));

	/* initial position is 0 */
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(stream)), ==, 0);

	/* read advances position */
	rc = g_input_stream_read(G_INPUT_STREAM(stream), buf, 3, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 3);
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(stream)), ==, 3);

	/* position is mirrored on the base stream */
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(base_stream)), ==, 3);

	/* seek the wrapper, verify base stream follows */
	ret = g_seekable_seek(G_SEEKABLE(stream), 5, G_SEEK_SET, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(stream)), ==, 5);
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(base_stream)), ==, 5);
}

static void
fu_stream_input_stream_truncate_func(void)
{
	gboolean ret;
	g_autoptr(GBytes) blob = g_bytes_new_static("ABCD", 4);
	g_autoptr(GError) error = NULL;
	g_autoptr(FuInputStream) base_stream = fu_memory_input_stream_new_from_bytes(blob);
	g_autoptr(FuInputStream) stream =
	    fu_stream_input_stream_from_stream(G_INPUT_STREAM(base_stream));

	g_assert_false(g_seekable_can_truncate(G_SEEKABLE(stream)));

	ret = g_seekable_truncate(G_SEEKABLE(stream), 2, NULL, &error);
	g_assert_false(ret);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
}

static void
fu_stream_input_stream_read_bytes_func(void)
{
	const guint8 *data;
	gsize sz = 0;
	g_autoptr(GBytes) blob = g_bytes_new_static("ABCDEFGH", 8);
	g_autoptr(GError) error = NULL;
	g_autoptr(FuInputStream) base_stream = fu_memory_input_stream_new_from_bytes(blob);
	g_autoptr(FuInputStream) stream =
	    fu_stream_input_stream_from_stream(G_INPUT_STREAM(base_stream));
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
fu_stream_input_stream_size_func(void)
{
	gboolean ret;
	gsize sz = 0;
	g_autoptr(GBytes) blob = g_bytes_new_static("ABCDEFGH", 8);
	g_autoptr(GError) error = NULL;
	g_autoptr(FuInputStream) base_stream = fu_memory_input_stream_new_from_bytes(blob);
	g_autoptr(FuInputStream) stream =
	    fu_stream_input_stream_from_stream(G_INPUT_STREAM(base_stream));

	ret = fu_input_stream_size(stream, &sz, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(sz, ==, 8);
}

static void
fu_stream_input_stream_read_safe_func(void)
{
	gboolean ret;
	guint8 buf[8] = {0};
	g_autoptr(GBytes) blob = g_bytes_new_static("ABCDEFGH", 8);
	g_autoptr(GError) error = NULL;
	g_autoptr(FuInputStream) base_stream = fu_memory_input_stream_new_from_bytes(blob);
	g_autoptr(FuInputStream) stream =
	    fu_stream_input_stream_from_stream(G_INPUT_STREAM(base_stream));

	/* read 3 bytes from offset 2 in the stream, store at offset 0 in buf */
	ret = fu_input_stream_read_safe(stream, buf, sizeof(buf), 0x0, 0x2, 3, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(buf[0], ==, 'C');
	g_assert_cmpint(buf[1], ==, 'D');
	g_assert_cmpint(buf[2], ==, 'E');
}

static void
fu_stream_input_stream_checksum_func(void)
{
	g_autoptr(GBytes) blob = g_bytes_new_static("ABCDEFGH", 8);
	g_autoptr(GError) error = NULL;
	g_autoptr(FuInputStream) base_stream = fu_memory_input_stream_new_from_bytes(blob);
	g_autoptr(FuInputStream) stream =
	    fu_stream_input_stream_from_stream(G_INPUT_STREAM(base_stream));
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
fu_stream_input_stream_empty_func(void)
{
	gboolean ret;
	gssize rc;
	gsize sz = 0;
	guint8 buf[4] = {0};
	g_autoptr(GBytes) blob = g_bytes_new_static("", 0);
	g_autoptr(GError) error = NULL;
	g_autoptr(FuInputStream) base_stream = fu_memory_input_stream_new_from_bytes(blob);
	g_autoptr(FuInputStream) stream =
	    fu_stream_input_stream_from_stream(G_INPUT_STREAM(base_stream));

	/* size should be 0 */
	ret = fu_input_stream_size(stream, &sz, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(sz, ==, 0);

	/* read should return 0 immediately */
	rc = g_input_stream_read(G_INPUT_STREAM(stream), buf, sizeof(buf), NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 0);

	/* tell should be at 0 */
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(stream)), ==, 0);
}

static void
fu_stream_input_stream_closed_base_func(void)
{
	gboolean ret;
	gssize rc;
	guint8 buf[4] = {0};
	g_autoptr(GBytes) blob = g_bytes_new_static("ABCDEFGH", 8);
	g_autoptr(GError) error = NULL;
	g_autoptr(FuInputStream) base_stream = fu_memory_input_stream_new_from_bytes(blob);
	g_autoptr(FuInputStream) stream =
	    fu_stream_input_stream_from_stream(G_INPUT_STREAM(base_stream));

	/* close the base stream */
	ret = g_input_stream_close(G_INPUT_STREAM(base_stream), NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* reading through the wrapper should fail */
	rc = g_input_stream_read(G_INPUT_STREAM(stream), buf, sizeof(buf), NULL, &error);
	g_assert_error(error, G_IO_ERROR, G_IO_ERROR_CLOSED);
	g_assert_cmpint(rc, ==, -1);
}

static void
fu_stream_input_stream_type_func(void)
{
	g_autoptr(GBytes) blob = g_bytes_new_static("ABCD", 4);
	g_autoptr(FuInputStream) base_stream = fu_memory_input_stream_new_from_bytes(blob);
	g_autoptr(FuInputStream) stream =
	    fu_stream_input_stream_from_stream(G_INPUT_STREAM(base_stream));

	g_assert_true(G_IS_INPUT_STREAM(stream));
	g_assert_true(G_IS_SEEKABLE(stream));
	g_assert_true(FU_IS_INPUT_STREAM(stream));
	g_assert_true(FU_IS_STREAM_INPUT_STREAM(stream));
}

static void
fu_stream_input_stream_seek_read_cycle_func(void)
{
	gboolean ret;
	gssize rc;
	guint8 buf[1] = {0};
	g_autoptr(GBytes) blob = g_bytes_new_static("ABCDEFGH", 8);
	g_autoptr(GError) error = NULL;
	g_autoptr(FuInputStream) base_stream = fu_memory_input_stream_new_from_bytes(blob);
	g_autoptr(FuInputStream) stream =
	    fu_stream_input_stream_from_stream(G_INPUT_STREAM(base_stream));

	/* read each byte individually by seeking */
	for (gsize i = 0; i < 8; i++) {
		ret = g_seekable_seek(G_SEEKABLE(stream), (goffset)i, G_SEEK_SET, NULL, &error);
		g_assert_no_error(error);
		g_assert_true(ret);

		rc = g_input_stream_read(G_INPUT_STREAM(stream), buf, 1, NULL, &error);
		g_assert_no_error(error);
		g_assert_cmpint(rc, ==, 1);
		g_assert_cmpint(buf[0], ==, 'A' + (guint8)i);
	}

	/* seek backwards and re-read */
	ret = g_seekable_seek(G_SEEKABLE(stream), 0, G_SEEK_SET, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	rc = g_input_stream_read(G_INPUT_STREAM(stream), buf, 1, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 1);
	g_assert_cmpint(buf[0], ==, 'A');
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/stream-input-stream/type", fu_stream_input_stream_type_func);
	g_test_add_func("/fwupd/stream-input-stream/read", fu_stream_input_stream_read_func);
	g_test_add_func("/fwupd/stream-input-stream/read-short",
			fu_stream_input_stream_read_short_func);
	g_test_add_func("/fwupd/stream-input-stream/seek", fu_stream_input_stream_seek_func);
	g_test_add_func("/fwupd/stream-input-stream/tell", fu_stream_input_stream_tell_func);
	g_test_add_func("/fwupd/stream-input-stream/truncate",
			fu_stream_input_stream_truncate_func);
	g_test_add_func("/fwupd/stream-input-stream/read-bytes",
			fu_stream_input_stream_read_bytes_func);
	g_test_add_func("/fwupd/stream-input-stream/size", fu_stream_input_stream_size_func);
	g_test_add_func("/fwupd/stream-input-stream/read-safe",
			fu_stream_input_stream_read_safe_func);
	g_test_add_func("/fwupd/stream-input-stream/checksum",
			fu_stream_input_stream_checksum_func);
	g_test_add_func("/fwupd/stream-input-stream/empty", fu_stream_input_stream_empty_func);
	g_test_add_func("/fwupd/stream-input-stream/closed-base",
			fu_stream_input_stream_closed_base_func);
	g_test_add_func("/fwupd/stream-input-stream/seek-read-cycle",
			fu_stream_input_stream_seek_read_cycle_func);
	return g_test_run();
}
