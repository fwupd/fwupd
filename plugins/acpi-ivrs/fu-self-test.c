/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 * Copyright 2022 Mario Limonciello <mario.limonciello@amd.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-acpi-ivrs.h"

static void
fu_acpi_ivrs_dma_remap_func(void)
{
	gboolean ret;
	guint32 rev;
	const gchar *oem_id;
	g_autoptr(FuAcpiIvrs) ivrs = fu_acpi_ivrs_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(GInputStream) stream = NULL;
	g_autofree gchar *fn = NULL;

	fn = g_test_build_filename(G_TEST_DIST, "tests", "IVRS-REMAP", NULL);
	if (!g_file_test(fn, G_FILE_TEST_EXISTS)) {
		g_test_skip("Missing IVRS-REMAP");
		return;
	}
	stream = fu_input_stream_from_path(fn, &error);
	g_assert_no_error(error);
	g_assert_nonnull(stream);
	ret = fu_firmware_parse_stream(FU_FIRMWARE(ivrs),
				       stream,
				       0x0,
				       FWUPD_INSTALL_FLAG_NONE,
				       &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_true(fu_acpi_ivrs_get_dma_remap(ivrs));

	rev = fu_acpi_table_get_revision(FU_ACPI_TABLE(ivrs));
	g_assert_cmpuint(rev, ==, 0x2);

	oem_id = fu_acpi_table_get_oem_id(FU_ACPI_TABLE(ivrs));
	g_assert_cmpstr(oem_id, ==, "LENOVO");

	oem_id = fu_acpi_table_get_oem_table_id(FU_ACPI_TABLE(ivrs));
	g_assert_cmpstr(oem_id, ==, "TP-R1K  ");

	rev = fu_acpi_table_get_oem_revision(FU_ACPI_TABLE(ivrs));
	g_assert_cmpuint(rev, ==, 2417033216);
}

static void
fu_acpi_ivrs_no_dma_remap_func(void)
{
	gboolean ret;
	guint32 rev;
	const gchar *oem_id;
	g_autoptr(FuAcpiIvrs) ivrs = fu_acpi_ivrs_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(GInputStream) stream = NULL;
	g_autofree gchar *fn = NULL;

	fn = g_test_build_filename(G_TEST_DIST, "tests", "IVRS-NOREMAP", NULL);
	if (!g_file_test(fn, G_FILE_TEST_EXISTS)) {
		g_test_skip("Missing IVRS-NOREMAP");
		return;
	}
	stream = fu_input_stream_from_path(fn, &error);
	g_assert_no_error(error);
	g_assert_nonnull(stream);
	ret = fu_firmware_parse_stream(FU_FIRMWARE(ivrs),
				       stream,
				       0x0,
				       FWUPD_INSTALL_FLAG_NONE,
				       &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_false(fu_acpi_ivrs_get_dma_remap(ivrs));

	rev = fu_acpi_table_get_revision(FU_ACPI_TABLE(ivrs));
	g_assert_cmpuint(rev, ==, 0x2);

	oem_id = fu_acpi_table_get_oem_id(FU_ACPI_TABLE(ivrs));
	g_assert_cmpstr(oem_id, ==, "LENOVO");

	oem_id = fu_acpi_table_get_oem_table_id(FU_ACPI_TABLE(ivrs));
	g_assert_cmpstr(oem_id, ==, "TC-S07  ");

	rev = fu_acpi_table_get_oem_revision(FU_ACPI_TABLE(ivrs));
	g_assert_cmpuint(rev, ==, 1074921472);
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
	g_test_add_func("/acpi-ivrs/dma-remap-support", fu_acpi_ivrs_dma_remap_func);
	g_test_add_func("/acpi-ivrs/no-dma-remap-support", fu_acpi_ivrs_no_dma_remap_func);

	return g_test_run();
}
