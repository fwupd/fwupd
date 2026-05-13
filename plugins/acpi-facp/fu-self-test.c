/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-acpi-facp.h"

static void
fu_acpi_facp_s2i_disabled_func(void)
{
	g_autofree gchar *fn = NULL;
	g_autoptr(FuAcpiFacp) facp = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;

	fn = g_test_build_filename(G_TEST_DIST, "tests", "FACP", NULL);
	if (!g_file_test(fn, G_FILE_TEST_EXISTS)) {
		g_test_skip("Missing FACP");
		return;
	}
	blob = fu_bytes_get_contents(fn, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob);
	facp = fu_acpi_facp_new(blob, &error);
	g_assert_no_error(error);
	g_assert_nonnull(facp);
	g_assert_false(fu_acpi_facp_get_s2i(facp));
	g_assert_cmpuint(fu_acpi_facp_get_pm_profile(facp), ==, FU_ACPI_FADT_PM_PROFILE_MOBILE);
}

static void
fu_acpi_facp_s2i_enabled_func(void)
{
	g_autofree gchar *fn = NULL;
	g_autoptr(FuAcpiFacp) facp = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;

	fn = g_test_build_filename(G_TEST_DIST, "tests", "FACP-S2I", NULL);
	if (!g_file_test(fn, G_FILE_TEST_EXISTS)) {
		g_test_skip("Missing FACP-S2I");
		return;
	}
	blob = fu_bytes_get_contents(fn, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob);
	facp = fu_acpi_facp_new(blob, &error);
	g_assert_no_error(error);
	g_assert_nonnull(facp);
	g_assert_true(fu_acpi_facp_get_s2i(facp));
	g_assert_cmpuint(fu_acpi_facp_get_pm_profile(facp), ==, FU_ACPI_FADT_PM_PROFILE_MOBILE);
}

static void
fu_acpi_facp_server_func(void)
{
	g_autofree gchar *fn = NULL;
	g_autoptr(FuAcpiFacp) facp = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;

	fn = g_test_build_filename(G_TEST_DIST, "tests", "FACP-SERVER", NULL);
	if (!g_file_test(fn, G_FILE_TEST_EXISTS)) {
		g_test_skip("Missing FACP-SERVER");
		return;
	}
	blob = fu_bytes_get_contents(fn, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob);
	facp = fu_acpi_facp_new(blob, &error);
	g_assert_no_error(error);
	g_assert_nonnull(facp);
	g_assert_false(fu_acpi_facp_get_s2i(facp));
	g_assert_cmpuint(fu_acpi_facp_get_pm_profile(facp),
			 ==,
			 FU_ACPI_FADT_PM_PROFILE_ENTERPRISE_SERVER);
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/acpi-facp/s2i-disabled", fu_acpi_facp_s2i_disabled_func);
	g_test_add_func("/acpi-facp/s2i-enabled", fu_acpi_facp_s2i_enabled_func);
	g_test_add_func("/acpi-facp/server", fu_acpi_facp_server_func);
	return g_test_run();
}
