/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <stdlib.h>
#include <string.h>

#include "fu-wacom-usb-common.h"
#include "fu-wacom-usb-firmware.h"
#include "fu-wacom-usb-struct.h"

static void
fu_wacom_usb_firmware_parse_func(void)
{
	gboolean ret;
	g_autofree gchar *fn = NULL;
	g_autoptr(FuFirmware) firmware = fu_wacom_usb_firmware_new();
	g_autoptr(FuFirmware) img = NULL;
	g_autoptr(GBytes) blob_block = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GFile) file = NULL;

	/* parse the test file */
	fn = g_test_build_filename(G_TEST_DIST, "tests", "test.wac", NULL);
	if (!g_file_test(fn, G_FILE_TEST_EXISTS)) {
		g_test_skip("no data file found");
		return;
	}
	file = g_file_new_for_path(fn);
	ret = fu_firmware_parse_file(firmware, file, FU_FIRMWARE_PARSE_FLAG_NO_SEARCH, &error);
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
	fu_wacom_usb_buffer_dump("IMG",
				 FU_WACOM_USB_REPORT_ID_MODULE,
				 g_bytes_get_data(blob_block, NULL),
				 g_bytes_get_size(blob_block));
}
static void
fu_wacom_usb_firmware_xml_func(void)
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
	filename = g_test_build_filename(G_TEST_DIST, "tests", "wacom-usb.builder.xml", NULL);
	ret = g_file_get_contents(filename, &xml_src, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	firmware1 = fu_firmware_new_from_xml(xml_src, &error);
	g_assert_no_error(error);
	g_assert_nonnull(firmware1);
	csum1 = fu_firmware_get_checksum(firmware1, G_CHECKSUM_SHA1, &error);
	g_assert_no_error(error);
	g_assert_cmpstr(csum1, ==, "346f6196449b356777cf241f6edb039d503b88a1");

	/* ensure we can round-trip */
	xml_out = fu_firmware_export_to_xml(firmware1, FU_FIRMWARE_EXPORT_FLAG_NONE, &error);
	g_assert_no_error(error);
	firmware2 = fu_firmware_new_from_xml(xml_out, &error);
	g_assert_no_error(error);
	g_assert_nonnull(firmware2);
	csum2 = fu_firmware_get_checksum(firmware2, G_CHECKSUM_SHA1, &error);
	g_assert_cmpstr(csum1, ==, csum2);
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);

	/* only critical and error are fatal */
	g_log_set_fatal_mask(NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);

	/* log everything */
	(void)g_setenv("G_MESSAGES_DEBUG", "all", FALSE);

	/* tests go here */
	g_type_ensure(FU_TYPE_WACOM_USB_FIRMWARE);
	g_test_add_func("/wacom-usb/firmware{parse}", fu_wacom_usb_firmware_parse_func);
	g_test_add_func("/wacom-usb/firmware{xml}", fu_wacom_usb_firmware_xml_func);
	return g_test_run();
}
