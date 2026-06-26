/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fwupd-test.h"

static void
fu_path_verify_safe_func(void)
{
	gboolean ret;
	g_autoptr(GError) error = NULL;
	const gchar *invalid[] = {
	    "",
	    ".",
	    "..",
	    "/etc/fstab",
	    "../../etc/fstab",
	    "foo/../bar",
	    "foo/..",
	    "foo\\bar",
	    "😀",
	};

	ret = fu_path_verify_safe("firmware.bin", &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_path_verify_safe("payload/firmware.bin", &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	for (guint i = 0; i < G_N_ELEMENTS(invalid); i++) {
		g_autoptr(GError) error_local = NULL;
		ret = fu_path_verify_safe(invalid[i], &error_local);
		g_assert_error(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
		g_assert_false(ret);
	}
}

static void
fu_path_sanitize_basename_func(void)
{
	struct {
		const gchar *input;
		const gchar *expected;
	} tests[] = {
	    {"normal", "normal"},
	    {"with/slash", "with_slash"},
	    {"with\\backslash", "with_backslash"},
	    {"../../../etc/passwd", "______etc_passwd"},
	    {"../../../../etc/cron.d", "________etc_cron.d"},
	    {".hidden", "_hidden"},
	    {".../etc/passwd", "_._etc_passwd"},
	    {"multiple///slashes", "multiple___slashes"},
	    {"valid-filename.bin", "valid-filename.bin"},
	    {"1.2.3.4", "1.2.3.4"},
	};

	for (guint i = 0; i < G_N_ELEMENTS(tests); i++) {
		g_autofree gchar *result = fu_path_sanitize_basename(tests[i].input);
		g_assert_cmpstr(result, ==, tests[i].expected);

		/* ensure sanitized output cannot be used for path traversal */
		g_assert_false(g_str_has_prefix(result, "/"));
		g_assert_false(g_str_has_prefix(result, ".."));
		g_assert_null(g_strstr_len(result, -1, "/"));
		g_assert_null(g_strstr_len(result, -1, "\\"));
	}
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/path/verify_safe", fu_path_verify_safe_func);
	g_test_add_func("/fwupd/path/sanitize_basename", fu_path_sanitize_basename_func);
	return g_test_run();
}
