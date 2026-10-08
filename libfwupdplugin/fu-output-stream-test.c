/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

static void
fu_output_stream_func(void)
{
	gboolean ret;
	gsize streamsz = 0;
	g_autofree gchar *fn = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GBytes) blob2 = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(FuInputStream) istream = NULL;
	g_autoptr(GOutputStream) ostream = NULL;
	const guint8 buf[] = {'h', 'e', 'l', 'l', 'o'};

	tmpdir = fu_temporary_directory_new("fu-output-stream-self-test", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);
	fn = fu_temporary_directory_build(tmpdir, "output.bin", NULL);

	/* write the blob to a file */
	ostream = fu_output_stream_from_path(fn, &error);
	g_assert_no_error(error);
	g_assert_nonnull(ostream);

	blob = g_bytes_new_static(buf, sizeof(buf));
	ret = fu_output_stream_write_bytes(ostream, blob, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = g_output_stream_close(ostream, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* read it back and verify the contents match */
	istream = fu_input_stream_from_path(fn, &error);
	g_assert_no_error(error);
	g_assert_nonnull(istream);
	ret = fu_input_stream_size(istream, &streamsz, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(streamsz, ==, sizeof(buf));

	blob2 = fu_input_stream_read_bytes(istream, 0, sizeof(buf), NULL, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob2);
	g_assert_true(fu_bytes_compare(blob, blob2, &error));
}

static void
fu_output_stream_path_invalid_func(void)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GOutputStream) ostream = NULL;

	/* parent directory does not exist */
	ostream = fu_output_stream_from_path("/this/does/not/exist/file.bin", &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_null(ostream);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/output-stream", fu_output_stream_func);
	g_test_add_func("/fwupd/output-stream/path-invalid", fu_output_stream_path_invalid_func);
	return g_test_run();
}
