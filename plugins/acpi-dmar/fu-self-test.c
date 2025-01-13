/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-acpi-dmar.h"

static void
fu_acpi_dmar_opt_in_func(void)
{
	gboolean ret;
	g_autoptr(FuAcpiDmar) dmar = fu_acpi_dmar_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(GInputStream) stream = NULL;
	g_autofree gchar *fn = NULL;

	fn = g_test_build_filename(G_TEST_DIST, "tests", "DMAR", NULL);
	if (!g_file_test(fn, G_FILE_TEST_EXISTS)) {
		g_test_skip("Missing DMAR");
		return;
	}
	stream = fu_input_stream_from_path(fn, &error);
	g_assert_no_error(error);
	g_assert_nonnull(stream);
	ret = fu_firmware_parse_stream(FU_FIRMWARE(dmar),
				       stream,
				       0x0,
				       FWUPD_INSTALL_FLAG_NONE,
				       &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_true(fu_acpi_dmar_get_opt_in(dmar));
}

static void
fu_acpi_dmar_opt_out_func(void)
{
	gboolean ret;
	g_autoptr(FuAcpiDmar) dmar = fu_acpi_dmar_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(GInputStream) stream = NULL;
	g_autofree gchar *fn = NULL;

	fn = g_test_build_filename(G_TEST_DIST, "tests", "DMAR-OPTOUT", NULL);
	if (!g_file_test(fn, G_FILE_TEST_EXISTS)) {
		g_test_skip("Missing DMAR-OPTOUT");
		return;
	}
	stream = fu_input_stream_from_path(fn, &error);
	g_assert_no_error(error);
	g_assert_nonnull(stream);
	ret = fu_firmware_parse_stream(FU_FIRMWARE(dmar),
				       stream,
				       0x0,
				       FWUPD_INSTALL_FLAG_NONE,
				       &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_false(fu_acpi_dmar_get_opt_in(dmar));
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);

	/* only critical and error are fatal */
	g_log_set_fatal_mask(NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);
	(void)g_setenv("G_MESSAGES_DEBUG", "all", TRUE);

	/* tests go here */
	g_test_add_func("/acpi-dmar/opt-in", fu_acpi_dmar_opt_in_func);
	g_test_add_func("/acpi-dmar/opt-out", fu_acpi_dmar_opt_out_func);

	return g_test_run();
}
