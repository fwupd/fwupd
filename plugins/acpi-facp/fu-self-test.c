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

static void
fu_acpi_facp_synthetic_func(void)
{
	/* a synthetic table so the test does not depend on captured hardware data */
	for (guint i = 0; i < 2; i++) {
		gboolean s2i = (i == 1);
		guint8 buf[0x74] = {0x0};
		g_autoptr(FuAcpiFacp) facp = NULL;
		g_autoptr(GBytes) blob = NULL;
		g_autoptr(GError) error = NULL;

		/* PM profile (offset 0x2D) and flags (offset 0x70) */
		buf[0x2D] = FU_ACPI_FADT_PM_PROFILE_MOBILE;
		if (s2i)
			buf[0x72] = 1 << (21 - 16); /* LOW_POWER_S0_IDLE_CAPABLE */

		blob = g_bytes_new(buf, sizeof(buf));
		facp = fu_acpi_facp_new(blob, &error);
		g_assert_no_error(error);
		g_assert_nonnull(facp);
		g_assert_cmpint(fu_acpi_facp_get_s2i(facp), ==, s2i);
		g_assert_cmpuint(fu_acpi_facp_get_pm_profile(facp),
				 ==,
				 FU_ACPI_FADT_PM_PROFILE_MOBILE);
	}
}

static void
fu_acpi_facp_truncated_func(void)
{
	guint8 buf[0x10] = {0x0};
	g_autoptr(FuAcpiFacp) facp = NULL;
	g_autoptr(GBytes) blob = g_bytes_new(buf, sizeof(buf));
	g_autoptr(GError) error = NULL;

	facp = fu_acpi_facp_new(blob, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_READ);
	g_assert_null(facp);
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/acpi-facp/s2i-disabled", fu_acpi_facp_s2i_disabled_func);
	g_test_add_func("/acpi-facp/s2i-enabled", fu_acpi_facp_s2i_enabled_func);
	g_test_add_func("/acpi-facp/server", fu_acpi_facp_server_func);
	g_test_add_func("/acpi-facp/synthetic", fu_acpi_facp_synthetic_func);
	g_test_add_func("/acpi-facp/truncated", fu_acpi_facp_truncated_func);
	return g_test_run();
}
