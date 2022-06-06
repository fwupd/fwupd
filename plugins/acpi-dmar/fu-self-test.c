/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-acpi-dmar.h"

static void
fu_acpi_dmar_opt_in_func(void)
{
	const gchar *ci = g_getenv("CI_NETWORK");
	g_autoptr(FuAcpiDmar) dmar = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autofree gchar *fn = NULL;

	fn = g_test_build_filename(G_TEST_DIST, "tests", "DMAR", NULL);
	if (!g_file_test(fn, G_FILE_TEST_EXISTS) && ci == NULL) {
		g_test_skip("Missing DMAR");
		return;
	}
	blob = fu_bytes_get_contents(fn, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob);
	dmar = fu_acpi_dmar_new(blob, &error);
	g_assert_no_error(error);
	g_assert_nonnull(dmar);
	g_assert_true(fu_acpi_dmar_get_opt_in(dmar));
}

static void
fu_acpi_dmar_opt_out_func(void)
{
	const gchar *ci = g_getenv("CI_NETWORK");
	g_autoptr(FuAcpiDmar) dmar = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autofree gchar *fn = NULL;

	fn = g_test_build_filename(G_TEST_DIST, "tests", "DMAR-OPTOUT", NULL);
	if (!g_file_test(fn, G_FILE_TEST_EXISTS) && ci == NULL) {
		g_test_skip("Missing DMAR-OPTOUT");
		return;
	}
	blob = fu_bytes_get_contents(fn, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob);
	dmar = fu_acpi_dmar_new(blob, &error);
	g_assert_no_error(error);
	g_assert_nonnull(dmar);
	g_assert_false(fu_acpi_dmar_get_opt_in(dmar));
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);

	/* only critical and error are fatal */
	g_log_set_fatal_mask(NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);
	(void)g_setenv("G_MESSAGES_DEBUG", "all", TRUE);

	/* tests go here */
	g_test_add_func("/acpi-dmar/opt-in", fu_acpi_dmar_opt_in_func);
	g_test_add_func("/acpi-dmar/opt-out", fu_acpi_dmar_opt_out_func);

	return g_test_run();
}
