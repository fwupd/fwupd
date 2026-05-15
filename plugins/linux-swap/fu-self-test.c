/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupd.h>

#include "fu-linux-swap.h"

static void
fu_linux_swap_none_func(void)
{
	g_autoptr(FuLinuxSwap) swap = NULL;
	g_autoptr(FuPathStore) pstore = fu_path_store_new();
	g_autoptr(GError) error = NULL;

	swap =
	    fu_linux_swap_new(pstore, "Filename\t\t\t\tType\t\tSize\tUsed\tPriority\n", 0, &error);
	g_assert_no_error(error);
	g_assert_nonnull(swap);
	g_assert_false(fu_linux_swap_get_enabled(swap));
	g_assert_false(fu_linux_swap_get_encrypted(swap));
}

static void
fu_linux_swap_plain_func(void)
{
	g_autoptr(FuLinuxSwap) swap = NULL;
	g_autoptr(FuPathStore) pstore = fu_path_store_new();
	g_autoptr(GError) error = NULL;

	swap =
	    fu_linux_swap_new(pstore,
			      "Filename\t\t\t\tType\t\tSize\tUsed\tPriority\n"
			      "/dev/nvme0n1p4                          partition\t5962748\t0\t-2\n",
			      0,
			      &error);
	if (g_error_matches(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND) ||
	    g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_NAME_HAS_NO_OWNER) ||
	    g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_SERVICE_UNKNOWN) ||
	    g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_SPAWN_EXEC_FAILED) ||
	    g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CONNECTION_REFUSED) ||
	    g_error_matches(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT) ||
	    g_error_matches(error, G_IO_ERROR, G_IO_ERROR_NOT_DIRECTORY) ||
	    g_error_matches(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
		g_test_skip(error->message);
		return;
	}
	g_assert_no_error(error);
	g_assert_nonnull(swap);
}

static void
fu_linux_swap_encrypted_func(void)
{
	g_autoptr(FuLinuxSwap) swap = NULL;
	g_autoptr(FuPathStore) pstore = fu_path_store_new();
	g_autoptr(GError) error = NULL;

	swap =
	    fu_linux_swap_new(pstore,
			      "Filename\t\t\t\tType\t\tSize\tUsed\tPriority\n"
			      "/dev/dm-1                               partition\t5962748\t0\t-2\n",
			      0,
			      &error);
	if (g_error_matches(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND) ||
	    g_error_matches(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND) ||
	    g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_SERVICE_UNKNOWN) ||
	    g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_SPAWN_EXEC_FAILED) ||
	    g_error_matches(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT)) {
		g_test_skip(error->message);
		return;
	}
	g_assert_no_error(error);
	g_assert_nonnull(swap);
}

static void
fu_linux_swap_dm_walker_lvm_on_luks_func(void)
{
	gboolean encrypted = FALSE;
	gboolean ret;
	g_autofree gchar *testdir = NULL;
	g_autoptr(FuPathStore) pstore = fu_path_store_new();
	g_autoptr(GError) error = NULL;

	testdir = g_test_build_filename(G_TEST_DIST, "tests", "lvm-on-luks", "sys", NULL);
	if (!g_file_test(testdir, G_FILE_TEST_IS_DIR)) {
		g_test_skip("missing fake-sysfs fixture");
		return;
	}
	fu_path_store_set_path(pstore, FU_PATH_KIND_SYSFSDIR, testdir);

	ret = fu_linux_swap_block_has_crypt_below(pstore, "dm-2", 0, &encrypted, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_true(encrypted);
}

static void
fu_linux_swap_dm_walker_direct_func(void)
{
	gboolean encrypted = FALSE;
	gboolean ret;
	g_autofree gchar *testdir = NULL;
	g_autoptr(FuPathStore) pstore = fu_path_store_new();
	g_autoptr(GError) error = NULL;

	testdir = g_test_build_filename(G_TEST_DIST, "tests", "lvm-on-luks", "sys", NULL);
	if (!g_file_test(testdir, G_FILE_TEST_IS_DIR)) {
		g_test_skip("missing fake-sysfs fixture");
		return;
	}
	fu_path_store_set_path(pstore, FU_PATH_KIND_SYSFSDIR, testdir);

	ret = fu_linux_swap_block_has_crypt_below(pstore, "dm-0", 0, &encrypted, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_true(encrypted);
}

static void
fu_linux_swap_dm_walker_plain_func(void)
{
	gboolean encrypted = FALSE;
	gboolean ret;
	g_autofree gchar *testdir = NULL;
	g_autoptr(FuPathStore) pstore = fu_path_store_new();
	g_autoptr(GError) error = NULL;

	testdir = g_test_build_filename(G_TEST_DIST, "tests", "plain", "sys", NULL);
	if (!g_file_test(testdir, G_FILE_TEST_IS_DIR)) {
		g_test_skip("missing fake-sysfs fixture");
		return;
	}
	fu_path_store_set_path(pstore, FU_PATH_KIND_SYSFSDIR, testdir);

	ret = fu_linux_swap_block_has_crypt_below(pstore, "sda1", 0, &encrypted, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_false(encrypted);
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/linux-swap/none", fu_linux_swap_none_func);
	g_test_add_func("/linux-swap/plain", fu_linux_swap_plain_func);
	g_test_add_func("/linux-swap/encrypted", fu_linux_swap_encrypted_func);
	g_test_add_func("/linux-swap/dm-walker/lvm-on-luks",
			fu_linux_swap_dm_walker_lvm_on_luks_func);
	g_test_add_func("/linux-swap/dm-walker/direct", fu_linux_swap_dm_walker_direct_func);
	g_test_add_func("/linux-swap/dm-walker/plain", fu_linux_swap_dm_walker_plain_func);
	return g_test_run();
}
