/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2022 Mario Limonciello <mario.limonciello@amd.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-acpi-ivrs.h"

static void
fu_acpi_ivrs_dma_remap_func(void)
{
	const gchar *ci = g_getenv("CI_NETWORK");
	g_autoptr(FuAcpiIvrs) ivrs = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autofree gchar *fn = NULL;

	fn = g_test_build_filename(G_TEST_DIST, "tests", "IVRS-REMAP", NULL);
	if (!g_file_test(fn, G_FILE_TEST_EXISTS) && ci == NULL) {
		g_test_skip("Missing IVRS-REMAP");
		return;
	}
	blob = fu_bytes_get_contents(fn, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob);
	ivrs = fu_acpi_ivrs_new(blob, &error);
	g_assert_no_error(error);
	g_assert_nonnull(ivrs);
	g_assert_true(fu_acpi_ivrs_get_dma_remap(ivrs));
}

static void
fu_acpi_ivrs_no_dma_remap_func(void)
{
	const gchar *ci = g_getenv("CI_NETWORK");
	g_autoptr(FuAcpiIvrs) ivrs = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autofree gchar *fn = NULL;

	fn = g_test_build_filename(G_TEST_DIST, "tests", "IVRS-NOREMAP", NULL);
	if (!g_file_test(fn, G_FILE_TEST_EXISTS) && ci == NULL) {
		g_test_skip("Missing IVRS-NOREMAP");
		return;
	}
	blob = fu_bytes_get_contents(fn, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob);
	ivrs = fu_acpi_ivrs_new(blob, &error);
	g_assert_no_error(error);
	g_assert_nonnull(ivrs);
	g_assert_false(fu_acpi_ivrs_get_dma_remap(ivrs));
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);

	/* only critical and error are fatal */
	g_log_set_fatal_mask(NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);
	(void)g_setenv("G_MESSAGES_DEBUG", "all", TRUE);

	/* tests go here */
	g_test_add_func("/acpi-ivrs/dma-remap-support", fu_acpi_ivrs_dma_remap_func);
	g_test_add_func("/acpi-ivrs/no-dma-remap-support", fu_acpi_ivrs_no_dma_remap_func);

	return g_test_run();
}
