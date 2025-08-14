/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-uf2-firmware.h"

static void
fu_uf2_firmware_xml_func(void)
{
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autofree gchar *csum1 = NULL;
	g_autofree gchar *csum2 = NULL;
	g_autofree gchar *xml_out = NULL;
	g_autofree gchar *xml_src = NULL;
	g_autoptr(FuFirmware) firmware1 = fu_uf2_firmware_new();
	g_autoptr(FuFirmware) firmware2 = fu_uf2_firmware_new();
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;

	/* build and write */
	filename = g_test_build_filename(G_TEST_DIST, "tests", "uf2.builder.xml", NULL);
	ret = g_file_get_contents(filename, &xml_src, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_firmware_build_from_xml(firmware1, xml_src, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	csum1 = fu_firmware_get_checksum(firmware1, G_CHECKSUM_SHA1, &error);
	g_assert_no_error(error);
	g_assert_cmpstr(csum1, ==, "4e130c6617496bee0dfbdff48f7248eccb1c696d");
	blob = fu_firmware_write(firmware1, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob);

	/* ensure we can round-trip */
	ret = fu_firmware_parse_bytes(firmware2, blob, 0x0, FU_FIRMWARE_PARSE_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	xml_out = fu_firmware_export_to_xml(firmware2, FU_FIRMWARE_EXPORT_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(xml_out);
	g_debug("%s", xml_out);
	csum2 = fu_firmware_get_checksum(firmware2, G_CHECKSUM_SHA1, &error);
	g_assert_cmpstr(csum1, ==, csum2);
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	(void)g_setenv("FWUPD_VERBOSE", "1", TRUE);
	g_test_init(&argc, &argv, NULL);

	/* only critical and error are fatal */
	g_log_set_fatal_mask(NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);

	/* tests go here */
	g_test_add_func("/uf2/firmware{xml}", fu_uf2_firmware_xml_func);
	return g_test_run();
}
