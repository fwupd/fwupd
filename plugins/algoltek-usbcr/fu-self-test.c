/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-algoltek-usbcr-firmware.h"

static void
fu_algoltek_usbcr_firmware_xml_func(void)
{
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autofree gchar *csum1 = NULL;
	g_autofree gchar *csum2 = NULL;
	g_autofree gchar *xml_out = NULL;
	g_autofree gchar *xml_src = NULL;
	g_autoptr(FuFirmware) firmware1 = NULL;
	g_autoptr(FuFirmware) firmware2 = NULL;
	g_autoptr(GError) error = NULL;

	/* build and write */
	filename = g_test_build_filename(G_TEST_DIST, "tests", "algoltek-usbcr.builder.xml", NULL);
	ret = g_file_get_contents(filename, &xml_src, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	firmware1 = fu_firmware_new_from_xml(xml_src, &error);
	g_assert_no_error(error);
	g_assert_nonnull(firmware1);
	csum1 = fu_firmware_get_checksum(firmware1, G_CHECKSUM_SHA1, &error);
	g_assert_no_error(error);
	g_assert_cmpstr(csum1, ==, "83bdad05844e42605637a5f5cdbfacb8afa65339");

	/* ensure we can round-trip */
	xml_out = fu_firmware_export_to_xml(firmware1, FU_FIRMWARE_EXPORT_FLAG_NONE, &error);
	g_assert_no_error(error);
	firmware2 = fu_firmware_new_from_xml(xml_out, &error);
	g_assert_no_error(error);
	g_assert_nonnull(fu_firmware_new_from_xml);
	csum2 = fu_firmware_get_checksum(firmware2, G_CHECKSUM_SHA1, &error);
	g_assert_cmpstr(csum1, ==, csum2);
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	(void)g_setenv("G_MESSAGES_DEBUG", "all", TRUE);

	g_test_init(&argc, &argv, NULL);

	/* only critical and error are fatal */
	g_log_set_fatal_mask(NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);

	/* tests go here */
	g_type_ensure(FU_TYPE_ALGOLTEK_USBCR_FIRMWARE);
	g_test_add_func("/fwupd/algoltek-usbcr/firmware{xml}", fu_algoltek_usbcr_firmware_xml_func);
	return g_test_run();
}
