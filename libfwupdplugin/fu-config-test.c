/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include <glib/gstdio.h>

#include "fu-config-private.h"
#include "fu-test.h"

static void
fu_config_func(void)
{
	GStatBuf statbuf = {0};
	gboolean ret;
	g_autofree gchar *composite_data = NULL;
	g_autoptr(FuConfig) config = fu_config_new();
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *fn_imu = NULL;
	g_autofree gchar *fn_mut = NULL;
	g_autofree gchar *path_imu = NULL;
	g_autofree gchar *path_mut = NULL;

#ifdef _WIN32
	/* the Windows file permission model is different than a simple octal value */
	g_test_skip("chmod not supported on Windows");
	return;
#endif

	/* deleted on error */
	tmpdir = fu_temporary_directory_new("config", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);

	/* immutable file */
	path_imu = fu_temporary_directory_build(tmpdir, "etc", "fwupd", NULL);
	(void)g_setenv("FWUPD_SYSCONFDIR", path_imu, TRUE);
	fn_imu = g_build_filename(path_imu, "fwupd.conf", NULL);
	g_assert_nonnull(fn_imu);
	ret = fu_path_mkdir_parent(fn_imu, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_remove(fn_imu);
	ret = g_file_set_contents(fn_imu,
				  "[fwupd]\n"
				  "Key=true\n",
				  -1,
				  &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	g_chmod(fn_imu, 0640);
	ret = g_stat(fn_imu, &statbuf);
	g_assert_cmpint(ret, ==, 0);
	g_assert_cmpint(statbuf.st_mode & 0777, ==, 0640);

	/* mutable file */
	path_mut = fu_temporary_directory_build(tmpdir, "var", "etc", "fwupd", NULL);
	(void)g_setenv("LOCALCONF_DIRECTORY", path_mut, TRUE);
	fn_mut = g_build_filename(path_mut, "fwupd.conf", NULL);
	g_assert_nonnull(fn_mut);
	ret = fu_path_mkdir_parent(fn_mut, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_remove(fn_mut);
	ret = g_file_set_contents(fn_mut,
				  "# group comment\n"
				  "[fwupd]\n"
				  "# key comment\n"
				  "Key=false\n",
				  -1,
				  &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	ret = fu_config_load(config, FU_CONFIG_LOAD_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	ret = fu_config_set_value(config, "fwupd", "Key", "false", &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	ret = g_file_get_contents(fn_mut, &composite_data, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_true(g_strstr_len(composite_data, -1, "Key=false") != NULL);
	g_assert_true(g_strstr_len(composite_data, -1, "Key=true") == NULL);
	g_assert_true(g_strstr_len(composite_data, -1, "# group comment") != NULL);
	g_assert_true(g_strstr_len(composite_data, -1, "# key comment") != NULL);
	g_remove(fn_mut);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/config", fu_config_func);
	return g_test_run();
}
