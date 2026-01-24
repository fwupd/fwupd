/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-test.h"

static void
fu_partial_input_stream_composite_func(void)
{
	gboolean ret;
	gint rc;
	guint8 buf[4] = {0};
	g_autoptr(GBytes) blob = g_bytes_new_static("12345678", 8);
	g_autoptr(GError) error = NULL;
	g_autoptr(GInputStream) composite_stream = fu_composite_input_stream_new();
	g_autoptr(GInputStream) partial_stream = NULL;

	fu_composite_input_stream_add_bytes(FU_COMPOSITE_INPUT_STREAM(composite_stream), blob);

	/* limit to '34' */
	partial_stream = fu_partial_input_stream_new(composite_stream, 2, 2, &error);
	g_assert_no_error(error);
	g_assert_nonnull(partial_stream);

	/* seek to the start of the partial input stream */
	ret = g_seekable_seek(G_SEEKABLE(partial_stream), 0x0, G_SEEK_SET, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(composite_stream)), ==, 0x2);
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(partial_stream)), ==, 0x0);

	/* read the 34 */
	rc = g_input_stream_read(partial_stream, buf, sizeof(buf), NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 2);
	g_assert_cmpint(buf[0], ==, '3');
	g_assert_cmpint(buf[1], ==, '4');
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(composite_stream)), ==, 0x4);
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(partial_stream)), ==, 0x2);

	/* there is no more data to read */
	rc = g_input_stream_read(partial_stream, buf, sizeof(buf), NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 0);
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(composite_stream)), ==, 0x4);
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(partial_stream)), ==, 0x2);
}

static void
fu_partial_input_stream_simple_func(void)
{
	gboolean ret;
	gssize rc;
	guint8 buf[2] = {0x0};
	g_autoptr(GBytes) blob = g_bytes_new_static("12345678", 8);
	g_autoptr(GError) error = NULL;
	g_autoptr(GInputStream) base_stream = g_memory_input_stream_new_from_bytes(blob);
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(GInputStream) stream2 = NULL;

	/* use G_MAXSIZE for "rest of the stream" */
	stream = fu_partial_input_stream_new(base_stream, 4, G_MAXSIZE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(stream);
	ret = g_seekable_seek(G_SEEKABLE(stream), 0x2, G_SEEK_SET, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(stream)), ==, 0x2);

	/* read from offset */
	rc = g_input_stream_read(stream, buf, 2, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 2);
	g_assert_cmpint(buf[0], ==, '7');
	g_assert_cmpint(buf[1], ==, '8');

	/* overflow */
	stream2 = fu_partial_input_stream_new(base_stream, 4, G_MAXSIZE - 1, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_null(stream2);
}

static void
fu_partial_input_stream_closed_base_func(void)
{
	gboolean ret;
	gssize rc;
	guint8 buf[2] = {0x0};
	g_autoptr(GBytes) blob = g_bytes_new_static("12345678", 8);
	g_autoptr(GError) error = NULL;
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(GInputStream) base_stream = g_memory_input_stream_new_from_bytes(blob);

	stream = fu_partial_input_stream_new(base_stream, 2, 4, &error);
	g_assert_no_error(error);
	g_assert_nonnull(stream);
	ret = g_input_stream_close(base_stream, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	ret = g_seekable_seek(G_SEEKABLE(stream), 0x0, G_SEEK_SET, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	rc = g_input_stream_read(stream, buf, sizeof(buf), NULL, &error);
	g_assert_error(error, G_IO_ERROR, G_IO_ERROR_CLOSED);
	g_assert_cmpint(rc, ==, -1);
}

static void
fu_partial_input_stream_func(void)
{
	gboolean ret;
	gssize rc;
	guint8 buf[5] = {0x0};
	goffset pos;
	g_autofree gchar *fn = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GBytes) blob = g_bytes_new_static("12345678", 8);
	/*                                             \--/   */
	g_autoptr(GBytes) blob2 = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GInputStream) base_stream = g_memory_input_stream_new_from_bytes(blob);
	g_autoptr(GInputStream) stream_complete = NULL;
	g_autoptr(GInputStream) stream_error = NULL;
	g_autoptr(GInputStream) stream_file = NULL;
	g_autoptr(GInputStream) stream = NULL;

	/* check the behavior of GFileInputStream */
	fn = g_test_build_filename(G_TEST_DIST, "tests", "dfu.builder.xml", NULL);
	g_assert_nonnull(fn);
	file = g_file_new_for_path(fn);
	stream_file = G_INPUT_STREAM(g_file_read(file, NULL, &error));
	g_assert_no_error(error);
	g_assert_nonnull(stream_file);
	ret = g_seekable_seek(G_SEEKABLE(stream_file), 0x0, G_SEEK_SET, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(stream_file)), ==, 0x0);
	ret = g_seekable_seek(G_SEEKABLE(stream_file), 0x0, G_SEEK_END, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(stream_file)), ==, 216);
	rc = g_input_stream_read(stream_file, buf, 2, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 0);
	pos = g_seekable_tell(G_SEEKABLE(stream_file));
	g_assert_cmpint(pos, ==, 216);
	ret = g_seekable_seek(G_SEEKABLE(stream_file), pos, G_SEEK_SET, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	rc = g_input_stream_read(stream_file, buf, 2, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 0);
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(stream_file)), ==, 216);
	/* we CAN seek past the end... */
	ret = g_seekable_seek(G_SEEKABLE(stream_file), pos + 10000, G_SEEK_SET, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(stream_file)), ==, 10216);
	/* reads all return zero */
	rc = g_input_stream_read(stream_file, buf, 2, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 0);
	/* END offset is negative */
	ret = g_seekable_seek(G_SEEKABLE(stream_file), -0x1, G_SEEK_END, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	rc = g_input_stream_read(stream_file, buf, 1, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 1);
	g_assert_cmpint(buf[0], ==, 10);

	/* check the behavior of GMemoryInputStream */
	g_assert_no_error(error);
	g_assert_nonnull(stream_file);
	ret = g_seekable_seek(G_SEEKABLE(base_stream), 0x0, G_SEEK_SET, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(base_stream)), ==, 0x0);
	ret = g_seekable_seek(G_SEEKABLE(base_stream), 0x0, G_SEEK_END, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(base_stream)), ==, 8);
	rc = g_input_stream_read(base_stream, buf, 2, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 0);
	pos = g_seekable_tell(G_SEEKABLE(base_stream));
	g_assert_cmpint(pos, ==, 8);
	ret = g_seekable_seek(G_SEEKABLE(base_stream), pos, G_SEEK_SET, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	rc = g_input_stream_read(base_stream, buf, 2, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 0);
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(base_stream)), ==, 8);
	/* we CANNOT seek past the end... */
	ret = g_seekable_seek(G_SEEKABLE(base_stream), pos + 10000, G_SEEK_SET, NULL, &error);
	g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
	g_assert_false(ret);
	g_clear_error(&error);
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(base_stream)), ==, 8);
	/* END offset is negative */
	ret = g_seekable_seek(G_SEEKABLE(base_stream), -1, G_SEEK_END, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	rc = g_input_stream_read(base_stream, buf, 1, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 1);
	g_assert_cmpint(buf[0], ==, '8');

	/* seek to non-start */
	stream = fu_partial_input_stream_new(base_stream, 2, 4, &error);
	g_assert_no_error(error);
	g_assert_nonnull(stream);
	ret = g_seekable_seek(G_SEEKABLE(stream), 0x2, G_SEEK_SET, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(stream)), ==, 0x2);

	/* read from start */
	rc = g_input_stream_read(stream, buf, 2, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 2);
	g_assert_cmpint(buf[0], ==, '5');
	g_assert_cmpint(buf[1], ==, '6');
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(stream)), ==, 0x4);
	rc = g_input_stream_read(stream, buf, 2, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 0);

	/* convert back to bytes */
	blob2 = fu_input_stream_read_bytes(stream, 0x0, G_MAXUINT32, NULL, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob2);
	g_assert_cmpint(g_bytes_get_size(blob2), ==, 4);

	/* seek to end of base stream */
	ret = g_seekable_seek(G_SEEKABLE(base_stream), 0x0, G_SEEK_END, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(base_stream)), ==, 0x8);
	rc = g_input_stream_read(base_stream, buf, 1, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 0);
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(base_stream)), ==, 0x8);

	/* seek to end of partial stream */
	ret = g_seekable_seek(G_SEEKABLE(stream), 0x0, G_SEEK_END, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(stream)), ==, 0x4);
	rc = g_input_stream_read(stream, buf, sizeof(buf), NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 0);

	/* seek to offset to end of partial stream */
	ret = g_seekable_seek(G_SEEKABLE(stream), -1, G_SEEK_END, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(stream)), ==, 0x3);
	rc = g_input_stream_read(stream, buf, sizeof(buf), NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 1);
	g_assert_cmpint(buf[0], ==, '6');

	/* attempt an overread of the base stream */
	ret = g_seekable_seek(G_SEEKABLE(stream), 0x2, G_SEEK_SET, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	rc = g_input_stream_read(stream, buf, sizeof(buf), NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 2);

	/* attempt to seek way past the base stream */
	ret = g_seekable_seek(G_SEEKABLE(stream), 0x1000, G_SEEK_SET, NULL, &error);
	g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
	g_assert_false(ret);
	g_clear_error(&error);

	/* read right up against the end of the base stream */
	stream_complete = fu_partial_input_stream_new(base_stream, 0, 8, &error);
	g_assert_no_error(error);
	g_assert_nonnull(stream_complete);
	ret = g_seekable_seek(G_SEEKABLE(stream_complete), 0x8, G_SEEK_SET, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	rc = g_input_stream_read(stream_complete, buf, sizeof(buf), NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 0);

	/* try to create an out-of-range partial stream */
	stream_error = fu_partial_input_stream_new(base_stream, 0, 9, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_null(stream_error);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/partial-input-stream", fu_partial_input_stream_func);
	g_test_add_func("/fwupd/partial-input-stream/closed-base",
			fu_partial_input_stream_closed_base_func);
	g_test_add_func("/fwupd/partial-input-stream/simple", fu_partial_input_stream_simple_func);
	g_test_add_func("/fwupd/partial-input-stream/composite",
			fu_partial_input_stream_composite_func);
	return g_test_run();
}
