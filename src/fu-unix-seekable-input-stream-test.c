/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fcntl.h>
#include <glib/gstdio.h>

#include "fu-unix-seekable-input-stream.h"

static void
fu_unix_seekable_input_stream_func(void)
{
#ifdef HAVE_GIO_UNIX
	gssize ret;
	gint fd;
	guint8 buf[6] = {0};
	g_autofree gchar *fn = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GInputStream) stream = NULL;

	fn = g_test_build_filename(G_TEST_DIST, "tests", "metadata.xml", NULL);
	g_assert_nonnull(fn);
	fd = g_open(fn, O_RDONLY, 0);
	g_assert_cmpint(fd, >=, 0);

	stream = fu_unix_seekable_input_stream_new(fd, TRUE);
	g_assert_nonnull(stream);

	/* first chuck */
	ret = g_input_stream_read(stream, buf, sizeof(buf) - 1, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(ret, ==, 5);
	g_assert_cmpstr((const gchar *)buf, ==, "<?xml");

	/* second chuck */
	ret = g_input_stream_read(stream, buf, sizeof(buf) - 1, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(ret, ==, 5);
	g_assert_cmpstr((const gchar *)buf, ==, " vers");

	/* first chuck, again */
	ret = g_seekable_seek(G_SEEKABLE(stream), 0, G_SEEK_SET, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(ret, ==, 1);
	ret = g_input_stream_read(stream, buf, sizeof(buf) - 1, NULL, &error);
	g_assert_no_error(error);
	g_assert_cmpint(ret, ==, 5);
	g_assert_cmpstr((const gchar *)buf, ==, "<?xml");
#else
	g_test_skip("No gio-unix-2.0 support, skipping");
#endif
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/unix-seekable-input-stream", fu_unix_seekable_input_stream_func);
	return g_test_run();
}
