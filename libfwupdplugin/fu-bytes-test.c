/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

static void
fu_bytes_get_data_func(void)
{
	const guint8 *buf;
	gboolean ret;
	g_autofree gchar *fn = NULL;
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(GBytes) bytes1 = NULL;
	g_autoptr(GBytes) bytes2 = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GMappedFile) mmap = NULL;

	/* deleted on error */
	tmpdir = fu_temporary_directory_new("bytes", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);

	/* create file with zero size */
	fn = fu_temporary_directory_build(tmpdir, "fwupdzero", NULL);
	ret = g_file_set_contents(fn, NULL, 0, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* check we got zero sized data */
	bytes1 = fu_bytes_get_contents(fn, &error);
	g_assert_no_error(error);
	g_assert_nonnull(bytes1);
	g_assert_cmpint(g_bytes_get_size(bytes1), ==, 0);
	g_assert_nonnull(g_bytes_get_data(bytes1, NULL));

	/* do the same with an mmap mapping, which returns NULL on empty file */
	mmap = g_mapped_file_new(fn, FALSE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(mmap);
	bytes2 = g_mapped_file_get_bytes(mmap);
	g_assert_nonnull(bytes2);
	g_assert_cmpint(g_bytes_get_size(bytes2), ==, 0);
	g_assert_null(g_bytes_get_data(bytes2, NULL));

	/* use the safe function */
	buf = fu_bytes_get_data_safe(bytes2, NULL, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_null(buf);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/bytes/get-data", fu_bytes_get_data_func);
	return g_test_run();
}
