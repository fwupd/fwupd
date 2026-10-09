/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

static void
fu_cpu_helper_cet_func(void)
{
	gboolean ret;
	gint exit_status;
	g_autofree gchar *fn = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GSubprocess) subprocess = NULL;

	/* this helper is only built on x86_64 with -fcf-protection */
	fn = g_test_build_filename(G_TEST_BUILT, "fwupd-detect-cet", NULL);
	if (!g_file_test(fn, G_FILE_TEST_EXISTS)) {
		g_test_skip("no fwupd-detect-cet helper");
		return;
	}

	subprocess = g_subprocess_new(G_SUBPROCESS_FLAGS_NONE, &error, fn, NULL);
	g_assert_no_error(error);
	g_assert_nonnull(subprocess);
	ret = g_subprocess_wait(subprocess, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* exits 0 when CET caught the violation, or 1 when CET is unavailable */
	g_assert_true(g_subprocess_get_if_exited(subprocess));
	exit_status = g_subprocess_get_exit_status(subprocess);
	g_assert_true(exit_status == 0 || exit_status == 1);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/cpu/helper-cet", fu_cpu_helper_cet_func);
	return g_test_run();
}
