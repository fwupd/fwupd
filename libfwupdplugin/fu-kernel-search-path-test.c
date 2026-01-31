/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-kernel-search-path-private.h"

static void
fu_kernel_search_path_func(void)
{
	gboolean ret;
	g_autofree gchar *search_path = NULL;
	g_autofree gchar *result1 = NULL;
	g_autofree gchar *result2 = NULL;
	g_autoptr(FuKernelSearchPathLocker) locker = NULL;
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(GError) error = NULL;

#ifndef __linux__
	g_test_skip("only works on Linux");
	return;
#endif

	/* deleted on error */
	tmpdir = fu_temporary_directory_new("kernel-search-path", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);
	search_path = fu_temporary_directory_build(tmpdir, "search_path", NULL);

	(void)g_setenv("FWUPD_FIRMWARESEARCH", "/dev/null", TRUE);
	result1 = fu_kernel_search_path_get_current(&error);
	g_assert_null(result1);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL);
	g_clear_error(&error);

	ret = g_file_set_contents(search_path, "oldvalue", -1, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	(void)g_setenv("FWUPD_FIRMWARESEARCH", search_path, TRUE);
	locker = fu_kernel_search_path_locker_new("/foo/bar", &error);
	g_assert_no_error(error);
	g_assert_nonnull(locker);
	g_assert_cmpstr(fu_kernel_search_path_locker_get_path(locker), ==, "/foo/bar");

	result1 = fu_kernel_search_path_get_current(&error);
	g_assert_nonnull(result1);
	g_assert_cmpstr(result1, ==, "/foo/bar");
	g_assert_no_error(error);
	g_clear_object(&locker);

	result2 = fu_kernel_search_path_get_current(&error);
	g_assert_nonnull(result2);
	g_assert_cmpstr(result2, ==, "oldvalue");
	g_assert_no_error(error);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/kernel-search-path", fu_kernel_search_path_func);
	return g_test_run();
}
