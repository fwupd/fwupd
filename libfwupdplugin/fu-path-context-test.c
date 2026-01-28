/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-path-context.h"

static void
fu_path_context_load(void)
{
	g_autoptr(FuPathContext) pathctx = fu_path_context_new();

	g_assert_cmpstr(fu_path_context_get_dir(pathctx, FU_PATH_KIND_DATADIR_PKG), ==, NULL);
	fu_path_context_set_dir(pathctx, FU_PATH_KIND_DATADIR_PKG, "/foo/bar");
	g_assert_cmpstr(fu_path_context_get_dir(pathctx, FU_PATH_KIND_DATADIR_PKG), ==, "/foo/bar");
	g_assert_cmpstr(fu_path_context_get_dir(pathctx, FU_PATH_KIND_EFIAPPDIR), ==, NULL);

	fu_path_context_load_defaults(pathctx);
	g_assert_cmpstr(fu_path_context_get_dir(pathctx, FU_PATH_KIND_HOSTFS_BOOT), ==, "/boot");

	g_setenv("FWUPD_PROCFS", "/tmp/proc", TRUE);
	fu_path_context_load_from_env(pathctx);
	g_assert_cmpstr(fu_path_context_get_dir(pathctx, FU_PATH_KIND_PROCFS), ==, "/tmp/proc");

	fu_path_context_set_dir(pathctx, FU_PATH_KIND_DATADIR_PKG, "/usr/share/fwupd");
	fu_path_context_add_prefix(pathctx, FU_PATH_KIND_DATADIR_PKG, "/snap");
	g_assert_cmpstr(fu_path_context_get_dir(pathctx, FU_PATH_KIND_DATADIR_PKG),
			==,
			"/snap/usr/share/fwupd");
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/path-context", fu_path_context_load);
	return g_test_run();
}
