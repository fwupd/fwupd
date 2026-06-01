/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include <glib/gstdio.h>

static void
fu_path_store_load(void)
{
	const gchar *dirname;
	g_autoptr(FuPathStore) pstore = fu_path_store_new();
	g_autoptr(GError) error = NULL;

	fu_path_store_set_path(pstore, FU_PATH_KIND_DATADIR_PKG, "/foo/bar");
	dirname = fu_path_store_get_path(pstore, FU_PATH_KIND_DATADIR_PKG, &error);
	g_assert_cmpstr(dirname, ==, "/foo/bar");

	dirname = fu_path_store_get_path(pstore, FU_PATH_KIND_EFIAPPDIR, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_null(dirname);
}

static void
fu_path_store_defaults(void)
{
	const gchar *dirname;
	g_autoptr(FuPathStore) pstore = fu_path_store_new();
	g_autoptr(GError) error = NULL;

	fu_path_store_load_defaults(pstore);
	fu_path_store_load_defaults(pstore);
	dirname = fu_path_store_get_path(pstore, FU_PATH_KIND_HOSTFS_BOOT, &error);
	g_assert_no_error(error);
	g_assert_nonnull(dirname);
	g_assert_cmpstr(dirname, ==, "/boot");
}

static void
fu_path_store_env(void)
{
	const gchar *dirname;
	g_autoptr(FuPathStore) pstore = fu_path_store_new();
	g_autoptr(GError) error = NULL;

	(void)g_setenv("FWUPD_LOCKDIR", "/tmp/lock", TRUE);

	fu_path_store_load_from_env(pstore);
	fu_path_store_load_from_env(pstore);
	dirname = fu_path_store_get_path(pstore, FU_PATH_KIND_LOCKDIR, &error);
	g_assert_no_error(error);
	g_assert_nonnull(dirname);
	g_assert_cmpstr(dirname, ==, "/tmp/lock");
}

static void
fu_path_store_prefix(void)
{
	const gchar *dirname;
	g_autoptr(FuPathStore) pstore = fu_path_store_new();
	g_autoptr(GError) error = NULL;

	fu_path_store_set_path(pstore, FU_PATH_KIND_DATADIR_PKG, "/usr/share/fwupd");
	fu_path_store_add_prefix(pstore, FU_PATH_KIND_DATADIR_PKG, "/snap");

	dirname = fu_path_store_get_path(pstore, FU_PATH_KIND_DATADIR_PKG, &error);
	g_assert_no_error(error);
	g_assert_nonnull(dirname);
	g_assert_cmpstr(dirname, ==, "/snap/usr/share/fwupd");
}

static void
fu_path_store_tmpdir(void)
{
	const gchar *dirname;
	g_autoptr(FuPathStore) pstore = fu_path_store_new();
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(GError) error = NULL;

	tmpdir = fu_temporary_directory_new("path-store", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);

	fu_path_store_set_tmpdir(pstore, FU_PATH_KIND_DATADIR_PKG, tmpdir);

	dirname = fu_path_store_get_path(pstore, FU_PATH_KIND_DATADIR_PKG, &error);
	g_assert_no_error(error);
	g_assert_nonnull(dirname);
}

static void
fu_path_store_find_program_func(void)
{
	gboolean ret;
	g_autofree gchar *fn = NULL;
	g_autofree gchar *fn_found = NULL;
	g_autofree gchar *fn_unfound = NULL;
	g_autoptr(FuPathStore) pstore = fu_path_store_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;

	tmpdir = fu_temporary_directory_new("program-path", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);
	fn = fu_temporary_directory_build(tmpdir, "test", NULL);
	ret = g_file_set_contents(fn, "#!/bin/bash", -1, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(g_chmod(fn, 0700), ==, 0);
	fu_path_store_add_program_path(pstore, "/not-going-to-exist");
	fu_path_store_add_program_path(pstore, fu_temporary_directory_get_path(tmpdir));
	fu_path_store_add_program_path(pstore, fu_temporary_directory_get_path(tmpdir));

	fn_found = fu_path_store_find_program(pstore, "test", &error);
	g_assert_no_error(error);
	g_assert_nonnull(fn_found);
	fn_unfound = fu_path_store_find_program(pstore, "not-going-to-exist", &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_null(fn_unfound);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/path-store", fu_path_store_load);
	g_test_add_func("/fwupd/path-store/defaults", fu_path_store_defaults);
	g_test_add_func("/fwupd/path-store/env", fu_path_store_env);
	g_test_add_func("/fwupd/path-store/prefix", fu_path_store_prefix);
	g_test_add_func("/fwupd/path-store/tmpdir", fu_path_store_tmpdir);
	g_test_add_func("/fwupd/path-store/find-program", fu_path_store_find_program_func);
	return g_test_run();
}
