/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fwupd-error.h"
#include "fwupd-stream-input-stream-private.h"

static void
fwupd_stream_input_stream_read_func(void)
{
	gssize rc;
	guint8 buf[8] = {0};
	g_autoptr(GBytes) blob = g_bytes_new_static("ABCDEFGH", 8);
	g_autoptr(GError) error = NULL;
	g_autoptr(GInputStream) base_stream =		/* nocheck:blocked */
	    g_memory_input_stream_new_from_bytes(blob); /* nocheck:blocked */
	g_autoptr(FwupdInputStream) stream =
	    FWUPD_INPUT_STREAM(fwupd_stream_input_stream_new(base_stream));

	/* read first 4 bytes */
	rc = fwupd_input_stream_read(stream, buf, 4, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 4);
	g_assert_cmpint(buf[0], ==, 'A');
	g_assert_cmpint(buf[1], ==, 'B');
	g_assert_cmpint(buf[2], ==, 'C');
	g_assert_cmpint(buf[3], ==, 'D');

	/* read next 4 bytes */
	rc = fwupd_input_stream_read(stream, buf, 4, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 4);
	g_assert_cmpint(buf[0], ==, 'E');
	g_assert_cmpint(buf[1], ==, 'F');
	g_assert_cmpint(buf[2], ==, 'G');
	g_assert_cmpint(buf[3], ==, 'H');

	/* reading past end returns 0 */
	rc = fwupd_input_stream_read(stream, buf, 4, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 0);
}

static void
fwupd_stream_input_stream_read_short_func(void)
{
	gssize rc;
	guint8 buf[16] = {0};
	g_autoptr(GBytes) blob = g_bytes_new_static("ABC", 3);
	g_autoptr(GInputStream) base_stream =		/* nocheck:blocked */
	    g_memory_input_stream_new_from_bytes(blob); /* nocheck:blocked */
	g_autoptr(FwupdInputStream) stream =
	    FWUPD_INPUT_STREAM(fwupd_stream_input_stream_new(base_stream));
	g_autoptr(GError) error = NULL;

	rc = fwupd_input_stream_read(stream, buf, sizeof(buf), NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 3);
	g_assert_cmpint(buf[0], ==, 'A');
	g_assert_cmpint(buf[1], ==, 'B');
	g_assert_cmpint(buf[2], ==, 'C');
}

static void
fwupd_stream_input_stream_seek_func(void)
{
	gboolean ret;
	gssize rc;
	guint8 buf[2] = {0};
	g_autoptr(GBytes) blob = g_bytes_new_static("ABCDEFGH", 8);
	g_autoptr(GError) error = NULL;
	g_autoptr(GInputStream) base_stream =		/* nocheck:blocked */
	    g_memory_input_stream_new_from_bytes(blob); /* nocheck:blocked */
	g_autoptr(FwupdInputStream) stream =
	    FWUPD_INPUT_STREAM(fwupd_stream_input_stream_new(base_stream));

	/* can_seek should return TRUE for a seekable base stream */
	g_assert_true(g_seekable_can_seek(G_SEEKABLE(stream)));

	/* SEEK_SET to offset 4 */
	ret = g_seekable_seek(G_SEEKABLE(stream), 4, G_SEEK_SET, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(stream)), ==, 4);

	rc = fwupd_input_stream_read(stream, buf, 2, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 2);
	g_assert_cmpint(buf[0], ==, 'E');
	g_assert_cmpint(buf[1], ==, 'F');

	/* SEEK_CUR forward by 1 (now at position 7) */
	ret = g_seekable_seek(G_SEEKABLE(stream), 1, G_SEEK_CUR, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(stream)), ==, 7);

	rc = fwupd_input_stream_read(stream, buf, 1, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 1);
	g_assert_cmpint(buf[0], ==, 'H');

	/* SEEK_END with negative offset */
	ret = g_seekable_seek(G_SEEKABLE(stream), -2, G_SEEK_END, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(stream)), ==, 6);

	rc = fwupd_input_stream_read(stream, buf, 2, NULL, &error);
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
fwupd_stream_input_stream_tell_func(void)
{
	gboolean ret;
	gssize rc;
	guint8 buf[3] = {0};
	g_autoptr(GBytes) blob = g_bytes_new_static("ABCDEF", 6);
	g_autoptr(GError) error = NULL;
	g_autoptr(GInputStream) base_stream =		/* nocheck:blocked */
	    g_memory_input_stream_new_from_bytes(blob); /* nocheck:blocked */
	g_autoptr(FwupdInputStream) stream =
	    FWUPD_INPUT_STREAM(fwupd_stream_input_stream_new(base_stream));

	/* initial position is 0 */
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(stream)), ==, 0);

	/* read advances position */
	rc = fwupd_input_stream_read(stream, buf, 3, NULL, &error);
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
fwupd_stream_input_stream_truncate_func(void)
{
	gboolean ret;
	g_autoptr(GBytes) blob = g_bytes_new_static("ABCD", 4);
	g_autoptr(GError) error = NULL;
	g_autoptr(GInputStream) base_stream =		/* nocheck:blocked */
	    g_memory_input_stream_new_from_bytes(blob); /* nocheck:blocked */
	g_autoptr(FwupdInputStream) stream =
	    FWUPD_INPUT_STREAM(fwupd_stream_input_stream_new(base_stream));

	g_assert_false(g_seekable_can_truncate(G_SEEKABLE(stream)));

	ret = g_seekable_truncate(G_SEEKABLE(stream), 2, NULL, &error);
	g_assert_false(ret);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
}

static void
fwupd_stream_input_stream_closed_base_func(void)
{
	gboolean ret;
	gssize rc;
	guint8 buf[4] = {0};
	g_autoptr(GBytes) blob = g_bytes_new_static("ABCDEFGH", 8);
	g_autoptr(GError) error = NULL;
	g_autoptr(GInputStream) base_stream =		/* nocheck:blocked */
	    g_memory_input_stream_new_from_bytes(blob); /* nocheck:blocked */
	g_autoptr(FwupdInputStream) stream =
	    FWUPD_INPUT_STREAM(fwupd_stream_input_stream_new(base_stream));

	/* close the base stream */
	ret = g_input_stream_close(G_INPUT_STREAM(base_stream), NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* reading through the wrapper should fail */
	rc = fwupd_input_stream_read(stream, buf, sizeof(buf), NULL, &error);
	g_assert_error(error, G_IO_ERROR, G_IO_ERROR_CLOSED);
	g_assert_cmpint(rc, ==, -1);
}

static void
fwupd_stream_input_stream_type_func(void)
{
	g_autoptr(GBytes) blob = g_bytes_new_static("ABCD", 4);
	g_autoptr(GInputStream) base_stream =		/* nocheck:blocked */
	    g_memory_input_stream_new_from_bytes(blob); /* nocheck:blocked */
	g_autoptr(FwupdInputStream) stream =
	    FWUPD_INPUT_STREAM(fwupd_stream_input_stream_new(base_stream));

	g_assert_false(G_IS_INPUT_STREAM(stream)); /* nocheck:blocked */
	g_assert_true(G_IS_SEEKABLE(stream));
	g_assert_true(FWUPD_IS_INPUT_STREAM(stream));
	g_assert_true(FWUPD_IS_STREAM_INPUT_STREAM(stream));
}

static void
fwupd_stream_input_stream_seek_read_cycle_func(void)
{
	gboolean ret;
	gssize rc;
	guint8 buf[1] = {0};
	g_autoptr(GBytes) blob = g_bytes_new_static("ABCDEFGH", 8);
	g_autoptr(GError) error = NULL;
	g_autoptr(GInputStream) base_stream =		/* nocheck:blocked */
	    g_memory_input_stream_new_from_bytes(blob); /* nocheck:blocked */
	g_autoptr(FwupdInputStream) stream =
	    FWUPD_INPUT_STREAM(fwupd_stream_input_stream_new(base_stream));

	/* read each byte individually by seeking */
	for (gsize i = 0; i < 8; i++) {
		ret = g_seekable_seek(G_SEEKABLE(stream), (goffset)i, G_SEEK_SET, NULL, &error);
		g_assert_no_error(error);
		g_assert_true(ret);

		rc = fwupd_input_stream_read(stream, buf, 1, NULL, &error);
		g_assert_no_error(error);
		g_assert_cmpint(rc, ==, 1);
		g_assert_cmpint(buf[0], ==, 'A' + (guint8)i);
	}

	/* seek backwards and re-read */
	ret = g_seekable_seek(G_SEEKABLE(stream), 0, G_SEEK_SET, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	rc = fwupd_input_stream_read(stream, buf, 1, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 1);
	g_assert_cmpint(buf[0], ==, 'A');
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/stream-input-stream/type", fwupd_stream_input_stream_type_func);
	g_test_add_func("/fwupd/stream-input-stream/read", fwupd_stream_input_stream_read_func);
	g_test_add_func("/fwupd/stream-input-stream/read-short",
			fwupd_stream_input_stream_read_short_func);
	g_test_add_func("/fwupd/stream-input-stream/seek", fwupd_stream_input_stream_seek_func);
	g_test_add_func("/fwupd/stream-input-stream/tell", fwupd_stream_input_stream_tell_func);
	g_test_add_func("/fwupd/stream-input-stream/truncate",
			fwupd_stream_input_stream_truncate_func);
	g_test_add_func("/fwupd/stream-input-stream/closed-base",
			fwupd_stream_input_stream_closed_base_func);
	g_test_add_func("/fwupd/stream-input-stream/seek-read-cycle",
			fwupd_stream_input_stream_seek_read_cycle_func);
	return g_test_run();
}
