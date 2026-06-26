/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-context-private.h"
#include "fu-uefi-sbat-device.h"
#include "fu-uefi-sbat-firmware.h"

static void
fu_uefi_sbat_firmware_parse_func(void)
{
	gboolean ret;
	const gchar *csv = "sbat,1,2021030218\ngrub,3\ngrub.debian,2\n";
	g_autoptr(FuFirmware) firmware = fu_uefi_sbat_firmware_new();
	g_autoptr(GBytes) blob = g_bytes_new_static(csv, strlen(csv));
	g_autoptr(GError) error = NULL;

	ret = fu_firmware_parse_bytes(firmware, blob, 0x0, FU_FIRMWARE_PARSE_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpstr(fu_firmware_get_version(firmware), ==, "1.3.2");
}

static void
fu_uefi_sbat_firmware_parse_single_func(void)
{
	gboolean ret;
	const gchar *csv = "sbat,4,2024010100\n";
	g_autoptr(FuFirmware) firmware = fu_uefi_sbat_firmware_new();
	g_autoptr(GBytes) blob = g_bytes_new_static(csv, strlen(csv));
	g_autoptr(GError) error = NULL;

	ret = fu_firmware_parse_bytes(firmware, blob, 0x0, FU_FIRMWARE_PARSE_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpstr(fu_firmware_get_version(firmware), ==, "4.0.0");
}

static void
fu_uefi_sbat_firmware_parse_multiple_func(void)
{
	gboolean ret;
	const gchar *csv = "sbat,1,2021030218\n"
			   "shim,2\n"
			   "grub,3\n"
			   "grub.debian,2\n"
			   "grub.fedora,4\n";
	g_autoptr(FuFirmware) firmware = fu_uefi_sbat_firmware_new();
	g_autoptr(GBytes) blob = g_bytes_new_static(csv, strlen(csv));
	g_autoptr(GError) error = NULL;

	ret = fu_firmware_parse_bytes(firmware, blob, 0x0, FU_FIRMWARE_PARSE_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpstr(fu_firmware_get_version(firmware), ==, "1.5.6");
}

static void
fu_uefi_sbat_device_new_func(void)
{
	const gchar *csv = "sbat,1,2021030218\ngrub,3\ngrub.debian,2\n";
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(GBytes) blob = g_bytes_new_static(csv, strlen(csv));
	g_autoptr(FuUefiSbatDevice) device = NULL;
	g_autoptr(GError) error = NULL;

	device = fu_uefi_sbat_device_new(ctx, blob, &error);
	g_assert_no_error(error);
	g_assert_nonnull(device);
	g_assert_cmpstr(fu_device_get_version(FU_DEVICE(device)), ==, "1.3.2");
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/uefi-sbat/firmware/parse", fu_uefi_sbat_firmware_parse_func);
	g_test_add_func("/uefi-sbat/firmware/parse-single",
			fu_uefi_sbat_firmware_parse_single_func);
	g_test_add_func("/uefi-sbat/firmware/parse-multiple",
			fu_uefi_sbat_firmware_parse_multiple_func);
	g_test_add_func("/uefi-sbat/device/new", fu_uefi_sbat_device_new_func);
	return g_test_run();
}
