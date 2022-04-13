/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-acpi-facp.h"

static void
fu_acpi_facp_s2i_disabled_func(void)
{
	const gchar *ci = g_getenv("CI_NETWORK");
	g_autofree gchar *fn = NULL;
	g_autoptr(FuAcpiFacp) facp = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;

	fn = g_test_build_filename(G_TEST_DIST, "tests", "FACP", NULL);
	if (!g_file_test(fn, G_FILE_TEST_EXISTS) && ci == NULL) {
		g_test_skip("Missing FACP");
		return;
	}
	blob = fu_common_get_contents_bytes(fn, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob);
	facp = fu_acpi_facp_new(blob, &error);
	g_assert_no_error(error);
	g_assert_nonnull(facp);
	g_assert_false(fu_acpi_facp_get_s2i(facp));
}

static void
fu_acpi_facp_s2i_enabled_func(void)
{
	const gchar *ci = g_getenv("CI_NETWORK");
	g_autofree gchar *fn = NULL;
	g_autoptr(FuAcpiFacp) facp = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;

	fn = g_test_build_filename(G_TEST_DIST, "tests", "FACP-S2I", NULL);
	if (!g_file_test(fn, G_FILE_TEST_EXISTS) && ci == NULL) {
		g_test_skip("Missing FACP-S2I");
		return;
	}
	blob = fu_common_get_contents_bytes(fn, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob);
	facp = fu_acpi_facp_new(blob, &error);
	g_assert_no_error(error);
	g_assert_nonnull(facp);
	g_assert_true(fu_acpi_facp_get_s2i(facp));
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);

	/* only critical and error are fatal */
	g_log_set_fatal_mask(NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);
	(void)g_setenv("G_MESSAGES_DEBUG", "all", TRUE);

	/* tests go here */
	g_test_add_func("/acpi-facp/s2i{disabled}", fu_acpi_facp_s2i_disabled_func);
	g_test_add_func("/acpi-facp/s2i{enabled}", fu_acpi_facp_s2i_enabled_func);
	return g_test_run();
}
