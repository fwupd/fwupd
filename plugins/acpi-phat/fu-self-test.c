/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-acpi-phat.h"

static void
fu_acpi_phat_parse_func(void)
{
	gboolean ret;
	g_autoptr(FuFirmware) phat = fu_acpi_phat_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autofree gchar *str = NULL;
	g_autofree gchar *fn = NULL;

	fn = g_test_build_filename(G_TEST_DIST, "tests", "PHAT", NULL);
	if (!g_file_test(fn, G_FILE_TEST_EXISTS)) {
		g_test_skip("missing PHAT");
		return;
	}
	blob = fu_bytes_get_contents(fn, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob);
	ret = fu_firmware_parse(phat,
				blob,
				FWUPD_INSTALL_FLAG_FORCE | FWUPD_INSTALL_FLAG_NO_SEARCH,
				&error);
	g_assert_no_error(error);
	g_assert_true(ret);
	str = fu_acpi_phat_to_report_string(FU_ACPI_PHAT(phat));
	g_print("%s\n", str);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);

	/* only critical and error are fatal */
	g_log_set_fatal_mask(NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);
	(void)g_setenv("G_MESSAGES_DEBUG", "all", TRUE);

	/* tests go here */
	g_test_add_func("/acpi-phat/parse", fu_acpi_phat_parse_func);

	return g_test_run();
}
