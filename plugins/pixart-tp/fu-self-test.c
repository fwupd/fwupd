/*
 * Copyright 2025 Harris Tai <harris_tai@pixart.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-pixart-tp-firmware.h"

static void
fu_pixart_tp_firmware_xml_func(void)
{
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autoptr(GError) error = NULL;

	filename = g_test_build_filename(G_TEST_DIST, "tests", "pixart-tp.builder.xml", NULL);
	ret = fu_firmware_roundtrip_from_filename(filename,
						  "a9ed17b970a867c190f62be59338dbad89d07553",
						  FU_FIRMWARE_BUILDER_FLAG_NO_BINARY_COMPARE,
						  &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_pixart_tp_firmware_pjp255_layout_func(void)
{
	g_autofree gchar *filename = NULL;
	g_autoptr(FuPixartTpFirmware) firmware = NULL;
	g_autoptr(FuStructPixartTpFirmwareHdr) st_hdr = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GInputStream) stream = NULL;
	FuPixartTpSection *fw_section = NULL;
	FuPixartTpSection *param_section = NULL;

	filename =
	    g_test_build_filename(G_TEST_DIST, "tests", "pixart-tp-pjp255.builder.xml", NULL);
	firmware = FU_PIXART_TP_FIRMWARE(fu_firmware_new_from_filename(filename, &error));
	g_assert_no_error(error);
	g_assert_nonnull(firmware);

	g_assert_cmpuint(fu_firmware_get_version_raw(FU_FIRMWARE(firmware)), ==, 0x2004);

	fw_section = fu_pixart_tp_firmware_find_section_by_type(firmware,
								FU_PIXART_TP_UPDATE_TYPE_FW_SECTION,
								&error);
	g_assert_no_error(error);
	g_assert_nonnull(fw_section);
	g_assert_cmpuint(fu_pixart_tp_section_get_crc(fw_section), ==, 0x0001fe04);

	param_section = fu_pixart_tp_firmware_find_section_by_type(firmware,
								   FU_PIXART_TP_UPDATE_TYPE_PARAM,
								   &error);
	g_assert_no_error(error);
	g_assert_nonnull(param_section);
	g_assert_cmpuint(fu_pixart_tp_section_get_target_flash_start(param_section), ==, 0xe000);
	g_assert_cmpuint(fu_pixart_tp_section_get_crc(param_section), ==, 0xc1c5ec96);

	blob = fu_firmware_write(FU_FIRMWARE(firmware), &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob);

	stream = g_memory_input_stream_new_from_bytes(blob);
	st_hdr = fu_struct_pixart_tp_firmware_hdr_parse_stream(stream, 0x0, &error);
	g_assert_no_error(error);
	g_assert_nonnull(st_hdr);
	g_assert_cmpuint(fu_struct_pixart_tp_firmware_hdr_get_header_ver(st_hdr), ==, 0x103);
	g_assert_cmpuint(fu_struct_pixart_tp_firmware_hdr_get_file_ver(st_hdr), ==, 0x2004);
	g_assert_cmpuint(fu_struct_pixart_tp_firmware_hdr_get_ic_part_id(st_hdr), ==, 0x0274);
	g_assert_cmpuint(fu_struct_pixart_tp_firmware_hdr_get_flash_sectors(st_hdr), ==, 0x000f);
	g_assert_cmpuint(fu_struct_pixart_tp_firmware_hdr_get_num_sections(st_hdr), ==, 2);
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	g_type_ensure(FU_TYPE_PIXART_TP_FIRMWARE);
	g_test_add_func("/pixart-tp/firmware/xml", fu_pixart_tp_firmware_xml_func);
	g_test_add_func("/pixart-tp/firmware/pjp255-layout",
			fu_pixart_tp_firmware_pjp255_layout_func);
	return g_test_run();
}
