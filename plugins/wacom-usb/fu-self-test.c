/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include <stdlib.h>
#include <string.h>

#include "fu-wac-common.h"
#include "fu-wac-firmware.h"

static void
fu_wac_firmware_parse_func(void)
{
	gboolean ret;
	g_autofree gchar *fn = NULL;
	g_autoptr(FuFirmware) firmware = fu_wac_firmware_new();
	g_autoptr(FuFirmware) img = NULL;
	g_autoptr(GBytes) blob_block = NULL;
	g_autoptr(GBytes) bytes = NULL;
	g_autoptr(GError) error = NULL;

	/* parse the test file */
	fn = g_test_build_filename(G_TEST_DIST, "tests", "test.wac", NULL);
	if (!g_file_test(fn, G_FILE_TEST_EXISTS)) {
		g_test_skip("no data file found");
		return;
	}
	bytes = fu_common_get_contents_bytes(fn, &error);
	g_assert_no_error(error);
	g_assert_nonnull(bytes);
	ret = fu_firmware_parse(firmware, bytes, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* get image data */
	img = fu_firmware_get_image_by_id(firmware, 0, &error);
	g_assert_no_error(error);
	g_assert_nonnull(img);

	/* get block */
	blob_block = fu_firmware_write_chunk(img, 0x8008000, 1024, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob_block);
	fu_wac_buffer_dump("IMG",
			   FU_WAC_REPORT_ID_MODULE,
			   g_bytes_get_data(blob_block, NULL),
			   g_bytes_get_size(blob_block));
}
static void
fu_wac_firmware_xml_func(void)
{
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autofree gchar *csum1 = NULL;
	g_autofree gchar *csum2 = NULL;
	g_autofree gchar *xml_out = NULL;
	g_autofree gchar *xml_src = NULL;
	g_autoptr(FuFirmware) firmware1 = fu_wac_firmware_new();
	g_autoptr(FuFirmware) firmware2 = fu_wac_firmware_new();
	g_autoptr(GError) error = NULL;

	/* build and write */
	filename = g_test_build_filename(G_TEST_DIST, "tests", "wacom-usb.builder.xml", NULL);
	ret = g_file_get_contents(filename, &xml_src, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_firmware_build_from_xml(firmware1, xml_src, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	csum1 = fu_firmware_get_checksum(firmware1, G_CHECKSUM_SHA1, &error);
	g_assert_no_error(error);
	g_assert_cmpstr(csum1, ==, "346f6196449b356777cf241f6edb039d503b88a1");

	/* ensure we can round-trip */
	xml_out = fu_firmware_export_to_xml(firmware1, FU_FIRMWARE_EXPORT_FLAG_NONE, &error);
	g_assert_no_error(error);
	ret = fu_firmware_build_from_xml(firmware2, xml_out, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	csum2 = fu_firmware_get_checksum(firmware2, G_CHECKSUM_SHA1, &error);
	g_assert_cmpstr(csum1, ==, csum2);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_type_ensure(FU_TYPE_SREC_FIRMWARE);

	/* only critical and error are fatal */
	g_log_set_fatal_mask(NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);

	/* log everything */
	(void)g_setenv("G_MESSAGES_DEBUG", "all", FALSE);

	/* tests go here */
	g_test_add_func("/wac/firmware{parse}", fu_wac_firmware_parse_func);
	g_test_add_func("/wac/firmware{xml}", fu_wac_firmware_xml_func);
	return g_test_run();
}
