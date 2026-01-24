/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-test.h"

static void
fu_kernel_cmdline_func(void)
{
	const gchar *buf = "key=val foo bar=\"baz baz baz\" tail\n";
	g_autoptr(GHashTable) hash = NULL;

	hash = fu_kernel_parse_cmdline(buf, strlen(buf));
	g_assert_nonnull(hash);
	g_assert_true(g_hash_table_contains(hash, "key"));
	g_assert_cmpstr(g_hash_table_lookup(hash, "key"), ==, "val");
	g_assert_true(g_hash_table_contains(hash, "foo"));
	g_assert_cmpstr(g_hash_table_lookup(hash, "foo"), ==, NULL);
	g_assert_true(g_hash_table_contains(hash, "bar"));
	g_assert_cmpstr(g_hash_table_lookup(hash, "bar"), ==, "baz baz baz");
	g_assert_true(g_hash_table_contains(hash, "tail"));
	g_assert_false(g_hash_table_contains(hash, ""));
}

static void
fu_kernel_config_func(void)
{
	const gchar *buf = "CONFIG_LOCK_DOWN_KERNEL_FORCE_NONE=y\n\n"
			   "# CONFIG_LOCK_DOWN_KERNEL_FORCE_INTEGRITY is not set\n";
	g_autoptr(GHashTable) hash = NULL;
	g_autoptr(GError) error = NULL;

	hash = fu_kernel_parse_config(buf, strlen(buf), &error);
	g_assert_no_error(error);
	g_assert_nonnull(hash);
	g_assert_true(g_hash_table_contains(hash, "CONFIG_LOCK_DOWN_KERNEL_FORCE_NONE"));
	g_assert_cmpstr(g_hash_table_lookup(hash, "CONFIG_LOCK_DOWN_KERNEL_FORCE_NONE"), ==, "y");
	g_assert_false(g_hash_table_contains(hash, "CONFIG_LOCK_DOWN_KERNEL_FORCE_INTEGRITY"));
}

static void
fu_kernel_lockdown_func(void)
{
	gboolean ret;
	g_autofree gchar *locked_dir = NULL;
	g_autofree gchar *none_dir = NULL;
	g_autofree gchar *old_kernel_dir = NULL;

#ifndef __linux__
	g_test_skip("only works on Linux");
	return;
#endif

	old_kernel_dir = g_test_build_filename(G_TEST_DIST, "tests", "lockdown", NULL);
	(void)g_setenv("FWUPD_SYSFSSECURITYDIR", old_kernel_dir, TRUE);
	ret = fu_kernel_locked_down();
	g_assert_false(ret);

	locked_dir = g_test_build_filename(G_TEST_DIST, "tests", "lockdown", "locked", NULL);
	(void)g_setenv("FWUPD_SYSFSSECURITYDIR", locked_dir, TRUE);
	ret = fu_kernel_locked_down();
	g_assert_true(ret);

	none_dir = g_test_build_filename(G_TEST_DIST, "tests", "lockdown", "none", NULL);
	(void)g_setenv("FWUPD_SYSFSSECURITYDIR", none_dir, TRUE);
	ret = fu_kernel_locked_down();
	g_assert_false(ret);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/kernel/cmdline", fu_kernel_cmdline_func);
	g_test_add_func("/fwupd/kernel/config", fu_kernel_config_func);
	g_test_add_func("/fwupd/kernel/lockdown", fu_kernel_lockdown_func);
	return g_test_run();
}
