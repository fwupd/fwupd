/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-context-private.h"
#include "fu-device-private.h"
#include "fu-genesys-usbhub-device.h"
#include "fu-genesys-usbhub-firmware.h"
#include "fu-genesys-usbhub-struct.h"

/* build a minimal GL3523 hub image with optional brand project tool string */
static GBytes *
fu_genesys_usbhub_firmware_test_build(FuGenesysTsVersion ts_version,
				      const gchar *ic_type,
				      const gchar *project)
{
	gboolean ret;
	g_autoptr(FuFirmware) firmware = g_object_new(FU_TYPE_GENESYS_USBHUB_FIRMWARE, NULL);
	g_autoptr(FwupdJsonObject) json_obj = fwupd_json_object_new();
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GError) error = NULL;

	fwupd_json_object_add_string(json_obj,
				     "tool_string_version",
				     fu_genesys_ts_version_to_string(ts_version));
	fwupd_json_object_add_string(json_obj, "mask_project_code", "ABCD");
	fwupd_json_object_add_string(json_obj, "mask_project_ic_type", ic_type);
	if (project != NULL)
		fwupd_json_object_add_string(json_obj, "brand_project", project);
	ret = fu_firmware_build(firmware, json_obj, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	fw = fu_firmware_write(firmware, &error);
	g_assert_no_error(error);
	g_assert_nonnull(fw);
	return g_steal_pointer(&fw);
}

static void
fu_genesys_usbhub_firmware_json_func(void)
{
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autoptr(GError) error = NULL;

	filename = g_test_build_filename(G_TEST_DIST, "tests", "genesys-usbhub.builder.json", NULL);
	ret = fu_firmware_roundtrip_from_filename(filename,
						  NULL,
						  FU_FIRMWARE_BUILDER_FLAG_NONE,
						  &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_genesys_usbhub_firmware_brand_project_func(void)
{
	gboolean ret;
	const gchar *tmp;
	g_autoptr(FuFirmware) firmware = g_object_new(FU_TYPE_GENESYS_USBHUB_FIRMWARE, NULL);
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GError) error = NULL;

	fw = fu_genesys_usbhub_firmware_test_build(FU_GENESYS_TS_VERSION_BRAND_PROJECT,
						   "352310",
						   "MYPROJECT");
	ret = fu_firmware_parse_bytes(firmware, fw, 0x0, FU_FIRMWARE_PARSE_FLAG_NO_SEARCH, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	tmp = fu_genesys_usbhub_firmware_get_brand_project(FU_GENESYS_USBHUB_FIRMWARE(firmware));
	g_assert_cmpstr(tmp, ==, "MYPROJECT");
	tmp = fu_genesys_usbhub_firmware_get_mask_project_ic_type(
	    FU_GENESYS_USBHUB_FIRMWARE(firmware));
	g_assert_cmpstr(tmp, ==, "352310");
}

static void
fu_genesys_usbhub_firmware_brand_project_old_func(void)
{
	gboolean ret;
	const gchar *tmp;
	g_autoptr(FuFirmware) firmware = g_object_new(FU_TYPE_GENESYS_USBHUB_FIRMWARE, NULL);
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GError) error = NULL;

	/* tool string too old to have a project, so it should be ignored */
	fw = fu_genesys_usbhub_firmware_test_build(FU_GENESYS_TS_VERSION_DYNAMIC_13_BYTE,
						   "352310",
						   "MYPROJECT");
	ret = fu_firmware_parse_bytes(firmware, fw, 0x0, FU_FIRMWARE_PARSE_FLAG_NO_SEARCH, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	tmp = fu_genesys_usbhub_firmware_get_brand_project(FU_GENESYS_USBHUB_FIRMWARE(firmware));
	g_assert_null(tmp);
	tmp = fu_genesys_usbhub_firmware_get_mask_project_ic_type(
	    FU_GENESYS_USBHUB_FIRMWARE(firmware));
	g_assert_cmpstr(tmp, ==, "352310");
}

static void
fu_genesys_usbhub_device_strict_project_check_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_QUIRKS);
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuFirmware) firmware = g_object_new(FU_TYPE_GENESYS_USBHUB_FIRMWARE, NULL);
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GError) error2 = NULL;

	fw = fu_genesys_usbhub_firmware_test_build(FU_GENESYS_TS_VERSION_BRAND_PROJECT,
						   "352310",
						   "MYPROJECT");
	ret = fu_firmware_parse_bytes(firmware, fw, 0x0, FU_FIRMWARE_PARSE_FLAG_NO_SEARCH, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* device has no project, so the strict check must refuse the firmware */
	device = g_object_new(FU_TYPE_GENESYS_USBHUB_DEVICE, "context", ctx, NULL);
	fu_device_add_private_flag(device, FU_GENESYS_USBHUB_FLAG_VERIFY_PROJECT);
	ret = fu_device_check_firmware(device, firmware, FU_FIRMWARE_PARSE_FLAG_NONE, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_assert_nonnull(strstr(error->message, "PROJECT"));
	g_assert_false(ret);

	/* without the flag the project is not checked, and it fails later for another reason */
	fu_device_remove_private_flag(device, FU_GENESYS_USBHUB_FLAG_VERIFY_PROJECT);
	ret = fu_device_check_firmware(device, firmware, FU_FIRMWARE_PARSE_FLAG_NONE, &error2);
	g_assert_error(error2, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_null(strstr(error2->message, "PROJECT"));
	g_assert_false(ret);
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	g_type_ensure(FU_TYPE_GENESYS_USBHUB_FIRMWARE);
	g_test_add_func("/genesys/usbhub-firmware/json", fu_genesys_usbhub_firmware_json_func);
	g_test_add_func("/genesys/usbhub-firmware/brand-project",
			fu_genesys_usbhub_firmware_brand_project_func);
	g_test_add_func("/genesys/usbhub-firmware/brand-project-old",
			fu_genesys_usbhub_firmware_brand_project_old_func);
	g_test_add_func("/genesys/usbhub-device/strict-project-check",
			fu_genesys_usbhub_device_strict_project_check_func);
	return g_test_run();
}
