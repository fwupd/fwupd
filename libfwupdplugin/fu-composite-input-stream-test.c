/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

static void
fu_composite_input_stream_func(void)
{
	gboolean ret;
	gsize streamsz = 0;
	gssize rc;
	guint8 buf[2] = {0x0};
	g_autofree gchar *str = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GBytes) blob1 = g_bytes_new_static("ab", 2);
	g_autoptr(GBytes) blob2 = g_bytes_new_static("cde", 3);
	g_autoptr(GBytes) blob3 = g_bytes_new_static("xxxfgyyy", 8);
	g_autoptr(GInputStream) composite_stream = fu_composite_input_stream_new();
	g_autoptr(GInputStream) stream3 = g_memory_input_stream_new_from_bytes(blob3);
	g_autoptr(GInputStream) stream4 = NULL;

	/* empty */
	ret = fu_input_stream_size(composite_stream, &streamsz, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(streamsz, ==, 0);

	/* add bytes */
	fu_composite_input_stream_add_bytes(FU_COMPOSITE_INPUT_STREAM(composite_stream), blob1);
	ret = fu_input_stream_size(composite_stream, &streamsz, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(streamsz, ==, 2);

	/* add bytes */
	fu_composite_input_stream_add_bytes(FU_COMPOSITE_INPUT_STREAM(composite_stream), blob2);
	ret = fu_input_stream_size(composite_stream, &streamsz, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(streamsz, ==, 5);

	/* add partial stream */
	stream4 = fu_partial_input_stream_new(stream3, 0x3, 2, &error);
	g_assert_no_error(error);
	g_assert_nonnull(stream4);
	fu_composite_input_stream_add_partial_stream(FU_COMPOSITE_INPUT_STREAM(composite_stream),
						     FU_PARTIAL_INPUT_STREAM(stream4));
	ret = fu_input_stream_size(composite_stream, &streamsz, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(streamsz, ==, 7);

	/* to string */
	str = fwupd_codec_to_string(FWUPD_CODEC(composite_stream));
	g_debug("%s", str);

	/* first block */
	ret = fu_input_stream_read_safe(composite_stream,
					buf,
					sizeof(buf),
					0x0, /* offset */
					0x0, /* seek */
					sizeof(buf),
					&error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(buf[0], ==, 'a');
	g_assert_cmpint(buf[1], ==, 'b');

	/* indented into second block */
	ret = fu_input_stream_read_safe(composite_stream,
					buf,
					sizeof(buf),
					0x0, /* offset */
					0x3, /* seek */
					sizeof(buf),
					&error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(buf[0], ==, 'd');
	g_assert_cmpint(buf[1], ==, 'e');

	/* third input stream has an offset */
	ret = fu_input_stream_read_safe(composite_stream,
					buf,
					sizeof(buf),
					0x0, /* offset */
					0x5, /* seek */
					sizeof(buf),
					&error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(buf[0], ==, 'f');
	g_assert_cmpint(buf[1], ==, 'g');

	/* read across a boundary, so should return early */
	ret = g_seekable_seek(G_SEEKABLE(composite_stream), 0x1, G_SEEK_SET, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	rc = g_input_stream_read(composite_stream, buf, 2, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 1);
	g_assert_cmpint(buf[0], ==, 'b');

	/* seek to end of composite stream */
	ret = g_seekable_seek(G_SEEKABLE(composite_stream), 0x0, G_SEEK_END, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(composite_stream)), ==, 0x7);
	rc = g_input_stream_read(composite_stream, buf, sizeof(buf), NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 0);

	/* seek to the same place directly */
	ret = g_seekable_seek(G_SEEKABLE(composite_stream), 0x7, G_SEEK_SET, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(composite_stream)), ==, 0x7);
	rc = g_input_stream_read(composite_stream, buf, sizeof(buf), NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 0);

	/* seek to offset to end of composite stream */
	ret = g_seekable_seek(G_SEEKABLE(composite_stream), -1, G_SEEK_END, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(g_seekable_tell(G_SEEKABLE(composite_stream)), ==, 0x6);
	rc = g_input_stream_read(composite_stream, buf, sizeof(buf), NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(rc, ==, 1);
	g_assert_cmpint(buf[0], ==, 'g');
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/composite-input-stream", fu_composite_input_stream_func);
	return g_test_run();
}
