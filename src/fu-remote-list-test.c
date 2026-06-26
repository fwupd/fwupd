/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-context-private.h"
#include "fu-remote-list.h"

static void
fu_remote_list_repair_func(void)
{
	gboolean ret;
	g_autofree gchar *testdatadir = NULL;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuRemoteList) remote_list = NULL;
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(FwupdRemote) remote1 = NULL;
	g_autoptr(FwupdRemote) remote2 = NULL;
	g_autoptr(FwupdRemote) remote3 = NULL;
	g_autoptr(GError) error = NULL;

	/* set up test harness */
	tmpdir = fu_temporary_directory_new("remote-list-repair", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);
	testdatadir = g_test_build_filename(G_TEST_DIST, "tests", NULL);
	fu_context_set_path(ctx, FU_PATH_KIND_DATADIR_PKG, testdatadir);
	fu_context_set_tmpdir(ctx, FU_PATH_KIND_CACHEDIR_PKG, tmpdir);
	fu_context_set_tmpdir(ctx, FU_PATH_KIND_LOCALSTATEDIR_METADATA, tmpdir);
	fu_config_set_default(fu_context_get_config(ctx), "fwupd", "TestDevices", "false");
	remote_list = fu_remote_list_new(ctx);

	fu_remote_list_set_lvfs_metadata_format(remote_list, "zst");
	ret = fu_remote_list_load(remote_list, FU_CONTEXT_LOAD_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* check .gz converted to .zst */
	remote1 = fu_remote_list_get_by_id(remote_list, "legacy-lvfs", &error);
	g_assert_no_error(error);
	g_assert_nonnull(remote1);
	g_assert_cmpstr(fwupd_remote_get_metadata_uri(remote1),
			==,
			"http://localhost/stable.xml.zst");

	/* check .xz converted to .zst */
	remote2 = fu_remote_list_get_by_id(remote_list, "legacy-lvfs-xz", &error);
	g_assert_no_error(error);
	g_assert_nonnull(remote2);
	g_assert_cmpstr(fwupd_remote_get_metadata_uri(remote2),
			==,
			"http://localhost/stable.xml.zst");

	/* check non-LVFS remote NOT .gz converted to .xz */
	remote3 = fu_remote_list_get_by_id(remote_list, "legacy", &error);
	g_assert_no_error(error);
	g_assert_nonnull(remote3);
	g_assert_cmpstr(fwupd_remote_get_metadata_uri(remote3),
			==,
			"http://localhost/stable.xml.gz");
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/remote-list/repair", fu_remote_list_repair_func);
	return g_test_run();
}
