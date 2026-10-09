/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-elantp-firmware.h"
#include "fu-elantp-haptic-firmware.h"

static void
fu_elantp_firmware_xml_func(void)
{
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autoptr(GError) error = NULL;

	filename = g_test_build_filename(G_TEST_DIST, "tests", "elantp.builder.xml", NULL);
	ret = fu_firmware_roundtrip_from_filename(filename,
						  "27056fc55254b1fda799b22b6339e62d2514fb6f",
						  FU_FIRMWARE_BUILDER_FLAG_NO_BINARY_COMPARE,
						  &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_elantp_haptic_firmware_func(void)
{
	gboolean ret;
	const guint8 buf[] = {0xFF, 0x40, 0xA2, 0x5B, 0x21, 0x03, 0x18};
	g_autoptr(FuFirmware) firmware = g_object_new(FU_TYPE_ELANTP_HAPTIC_FIRMWARE, NULL);
	g_autoptr(GBytes) blob = g_bytes_new(buf, sizeof(buf));
	g_autoptr(GError) error = NULL;

	ret = fu_firmware_parse_bytes(firmware, blob, 0x0, FU_FIRMWARE_PARSE_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpstr(fu_firmware_get_version(firmware), ==, "24010302");
	g_assert_cmpint(
	    fu_elantp_haptic_firmware_get_driver_ic(FU_ELANTP_HAPTIC_FIRMWARE(firmware)),
	    ==,
	    0x2);
}

static void
fu_elantp_haptic_firmware_invalid_func(void)
{
	gboolean ret;
	/* valid magic but a 0xFF version component */
	const guint8 buf[] = {0xFF, 0x40, 0xA2, 0x5B, 0x21, 0x03, 0xFF};
	g_autoptr(FuFirmware) firmware = g_object_new(FU_TYPE_ELANTP_HAPTIC_FIRMWARE, NULL);
	g_autoptr(GBytes) blob = g_bytes_new(buf, sizeof(buf));
	g_autoptr(GError) error = NULL;

	ret = fu_firmware_parse_bytes(firmware, blob, 0x0, FU_FIRMWARE_PARSE_FLAG_NONE, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_assert_false(ret);
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	g_type_ensure(FU_TYPE_ELANTP_FIRMWARE);
	g_test_add_func("/elantp/firmware/xml", fu_elantp_firmware_xml_func);
	g_test_add_func("/elantp/haptic-firmware", fu_elantp_haptic_firmware_func);
	g_test_add_func("/elantp/haptic-firmware/invalid", fu_elantp_haptic_firmware_invalid_func);
	return g_test_run();
}
