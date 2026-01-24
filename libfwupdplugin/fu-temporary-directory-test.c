/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

static void
fu_temporary_directory_func(void)
{
	const gchar *tmpdir_path;
	const gchar *tmpdir_fn;
	g_autofree gchar *tmpdir_path_copy = NULL;
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(GError) error = NULL;

	tmpdir = fu_temporary_directory_new("foobar", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);

	tmpdir_path = fu_temporary_directory_get_path(tmpdir);
	g_assert_nonnull(tmpdir_path);
	g_assert_true(g_strstr_len(tmpdir_path, -1, "foobar") != NULL);
	g_assert_true(g_file_test(tmpdir_path, G_FILE_TEST_IS_DIR));

	tmpdir_fn = fu_temporary_directory_build(tmpdir, "baz", NULL);
	tmpdir_path_copy = g_strdup(tmpdir_path);
	g_clear_object(&tmpdir);
	g_assert_false(g_file_test(tmpdir_fn, G_FILE_TEST_EXISTS));
	g_assert_false(g_file_test(tmpdir_path_copy, G_FILE_TEST_IS_DIR));
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/temporary-directory", fu_temporary_directory_func);
	return g_test_run();
}
