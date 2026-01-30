/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

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

	g_setenv("FWUPD_LOCKDIR", "/tmp/lock", TRUE);

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

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/path-store", fu_path_store_load);
	g_test_add_func("/fwupd/path-store/defaults", fu_path_store_defaults);
	g_test_add_func("/fwupd/path-store/env", fu_path_store_env);
	g_test_add_func("/fwupd/path-store/prefix", fu_path_store_prefix);
	g_test_add_func("/fwupd/path-store/tmpdir", fu_path_store_tmpdir);
	return g_test_run();
}
