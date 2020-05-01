/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupd.h>

#include "fu-common.h"

#include "fu-acpi-facp.h"

static void
fu_acpi_facp_s2i_disabled_func (void)
{
	g_autofree gchar *fn = NULL;
	g_autoptr(FuAcpiFacp) facp = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;

	fn = g_build_filename (TESTDATADIR, "FACP", NULL);
	blob = fu_common_get_contents_bytes (fn, &error);
	g_assert_no_error (error);
	g_assert_nonnull (blob);
	facp = fu_acpi_facp_new (blob, &error);
	g_assert_no_error (error);
	g_assert_nonnull (facp);
	g_assert_false (fu_acpi_facp_get_s2i (facp));
}

static void
fu_acpi_facp_s2i_enabled_func (void)
{
	g_autofree gchar *fn = NULL;
	g_autoptr(FuAcpiFacp) facp = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;

	fn = g_build_filename (TESTDATADIR, "FACP-S2I", NULL);
	blob = fu_common_get_contents_bytes (fn, &error);
	g_assert_no_error (error);
	g_assert_nonnull (blob);
	facp = fu_acpi_facp_new (blob, &error);
	g_assert_no_error (error);
	g_assert_nonnull (facp);
	g_assert_true (fu_acpi_facp_get_s2i (facp));
}

int
main (int argc, char **argv)
{
	g_test_init (&argc, &argv, NULL);

	/* only critical and error are fatal */
	g_log_set_fatal_mask (NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);
	g_setenv ("G_MESSAGES_DEBUG", "all", TRUE);

	/* tests go here */
	g_test_add_func ("/acpi-facp/s2i{disabled}", fu_acpi_facp_s2i_disabled_func);
	g_test_add_func ("/acpi-facp/s2i{enabled}", fu_acpi_facp_s2i_enabled_func);
	return g_test_run ();
}
