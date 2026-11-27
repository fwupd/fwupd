/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-xy-kbms-common.h"
#include "fu-xy-kbms-firmware.h"
#include "fu-xy-kbms-pinmap.h"
#include "fu-xy-kbms-struct.h"

static gboolean
fu_xy_kbms_pinmap_parse_text(const gchar *text, GError **error)
{
	g_autoptr(FuFirmware) firmware = fu_xy_kbms_pinmap_new();
	g_autoptr(GBytes) blob = g_bytes_new(text, strlen(text));
	return fu_firmware_parse_bytes(firmware, blob, 0x0, FU_FIRMWARE_PARSE_FLAG_NONE, error);
}

static void
fu_xy_kbms_pinmap_func(void)
{
	gboolean ret;
	g_autofree gchar *checksum = NULL;
	g_autofree gchar *filename = NULL;
	g_autoptr(FuFirmware) firmware = fu_xy_kbms_pinmap_new();
	g_autoptr(FuFirmware) firmware2 = fu_xy_kbms_pinmap_new();
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GBytes) fw2 = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GFile) file = NULL;

	filename =
	    g_test_build_filename(G_TEST_DIST, "tests", "xykbms_pin_usb_default_uk.cfg", NULL);
	file = g_file_new_for_path(filename);
	ret = fu_firmware_parse_file(firmware, file, FU_FIRMWARE_PARSE_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(fu_firmware_get_size(firmware), ==, FU_STRUCT_XY_KBMS_CONFIG_SIZE_PINMAP);
	fw = fu_firmware_get_bytes(firmware, &error);
	g_assert_no_error(error);
	g_assert_nonnull(fw);
	checksum = g_compute_checksum_for_bytes(G_CHECKSUM_SHA1, fw);
	g_assert_cmpstr(checksum, ==, "5fd53596fc24ff285cda5e7a2bbb752dbbef0141");

	/* the text writer parses back to the same pinmap */
	blob = fu_firmware_write(firmware, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob);
	ret = fu_firmware_parse_bytes(firmware2, blob, 0x0, FU_FIRMWARE_PARSE_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	fw2 = fu_firmware_get_bytes(firmware2, &error);
	g_assert_no_error(error);
	g_assert_nonnull(fw2);
	g_assert_true(g_bytes_equal(fw, fw2));
}

static void
fu_xy_kbms_pinmap_invalid_func(void)
{
	gboolean ret;
	g_autoptr(GError) error1 = NULL;
	g_autoptr(GError) error2 = NULL;
	g_autoptr(GError) error3 = NULL;

	/* three hex digits */
	ret = fu_xy_kbms_pinmap_parse_text("0x12 0x123\n", &error1);
	g_assert_error(error1, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_false(ret);

	/* comments are ignored, so this is too short */
	ret = fu_xy_kbms_pinmap_parse_text("0x12 ; 0x1234\n0xaa\n", &error2);
	g_assert_error(error2, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_false(ret);

	/* empty */
	ret = fu_xy_kbms_pinmap_parse_text("", &error3);
	g_assert_error(error3, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_false(ret);
}

static void
fu_xy_kbms_pinmap_sentinel_func(void)
{
	gboolean ret;
	g_autoptr(GError) error = NULL;
	g_autoptr(GString) str = g_string_new(NULL);

	/* correct size, but no 0xaa 0xbb 0xcc 0xdd marker */
	for (guint i = 0; i < FU_STRUCT_XY_KBMS_CONFIG_SIZE_PINMAP - 1; i++)
		g_string_append(str, "0x00\n");
	g_string_append_printf(str, "0x%02x\n", (guint)FU_XY_KBMS_PINMAP_VERSION);
	ret = fu_xy_kbms_pinmap_parse_text(str->str, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_false(ret);
}

static void
fu_xy_kbms_config_crc_func(void)
{
	gboolean ret;
	g_autoptr(FuStructXyKbmsConfig) st = fu_struct_xy_kbms_config_new();
	g_autoptr(GError) error = NULL;

	/* standard CRC-32, same as zlib */
	g_assert_cmpint(fu_xy_kbms_config_crc((const guint8 *)"123456789", 9), ==, 0xCBF43926);

	/* invalid CRC */
	ret = fu_xy_kbms_config_validate(st->buf, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_false(ret);
	g_clear_error(&error);

	/* fixed up, and only the first 0x80 bytes are covered by the CRC */
	ret = fu_xy_kbms_config_update_crc(st->buf, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_memwrite_uint8_safe(st->buf->data, st->buf->len, 0x100, 0xFF, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_xy_kbms_config_validate(st->buf, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* wrong size */
	g_byte_array_set_size(st->buf, st->buf->len - 1);
	ret = fu_xy_kbms_config_validate(st->buf, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_false(ret);
}

static void
fu_xy_kbms_firmware_func(void)
{
	gboolean ret;
	g_autoptr(FuFirmware) firmware1 = fu_xy_kbms_firmware_new();
	g_autoptr(FuFirmware) firmware2 = fu_xy_kbms_firmware_new();
	g_autoptr(GBytes) blob1 = NULL;
	g_autoptr(GBytes) blob2 = NULL;
	g_autoptr(GError) error1 = NULL;
	g_autoptr(GError) error2 = NULL;

	/* aligned */
	blob1 = g_bytes_new_take(g_malloc0(0x1000), 0x1000);
	ret = fu_firmware_parse_bytes(firmware1, blob1, 0x0, FU_FIRMWARE_PARSE_FLAG_NONE, &error1);
	g_assert_no_error(error1);
	g_assert_true(ret);

	/* not aligned */
	blob2 = g_bytes_new_take(g_malloc0(0x1001), 0x1001);
	ret = fu_firmware_parse_bytes(firmware2, blob2, 0x0, FU_FIRMWARE_PARSE_FLAG_NONE, &error2);
	g_assert_error(error2, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_assert_false(ret);
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	g_type_ensure(FU_TYPE_XY_KBMS_FIRMWARE);
	g_type_ensure(FU_TYPE_XY_KBMS_PINMAP);
	g_test_add_func("/xy-kbms/pinmap", fu_xy_kbms_pinmap_func);
	g_test_add_func("/xy-kbms/pinmap/invalid", fu_xy_kbms_pinmap_invalid_func);
	g_test_add_func("/xy-kbms/pinmap/sentinel", fu_xy_kbms_pinmap_sentinel_func);
	g_test_add_func("/xy-kbms/config/crc", fu_xy_kbms_config_crc_func);
	g_test_add_func("/xy-kbms/firmware", fu_xy_kbms_firmware_func);
	return g_test_run();
}
