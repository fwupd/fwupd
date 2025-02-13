/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-bitmap-image.h"
#include "fu-context-private.h"
#include "fu-uefi-bgrt.h"
#include "fu-uefi-capsule-backend.h"
#include "fu-uefi-cod-device.h"
#include "fu-uefi-common.h"
#include "fu-volume-private.h"

static void
fu_uefi_update_esp_valid_func(void)
{
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuVolume) volume_esp = fu_volume_new_from_mount_path("/tmp");
	g_autoptr(GBytes) blob = g_bytes_new_static((const guint8 *)"BOB", 3);
	g_autoptr(GBytes) blob_padded = fu_bytes_pad(blob, 4 * 1024 * 1024, 0xFF);
	g_autoptr(GError) error = NULL;
	g_autoptr(GInputStream) stream = g_memory_input_stream_new_from_bytes(blob_padded);

	/* enough to fit the firmware */
	fu_volume_set_filesystem_free(volume_esp, 10 * 1024 * 1024);

	device = g_object_new(FU_TYPE_UEFI_CAPSULE_DEVICE, "context", ctx, NULL);
	fu_uefi_capsule_device_set_esp(FU_UEFI_CAPSULE_DEVICE(device), volume_esp);
	firmware =
	    fu_device_prepare_firmware(device, stream, progress, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(firmware);
}

static void
fu_uefi_update_esp_invalid_func(void)
{
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuVolume) volume_esp = fu_volume_new_from_mount_path("/tmp");
	g_autoptr(GBytes) blob = g_bytes_new_static((const guint8 *)"BOB", 3);
	g_autoptr(GBytes) blob_padded = fu_bytes_pad(blob, 4 * 1024 * 1024, 0xFF);
	g_autoptr(GError) error = NULL;
	g_autoptr(GInputStream) stream = g_memory_input_stream_new_from_bytes(blob_padded);

	/* enough to fit the firmware */
	fu_volume_set_filesystem_free(volume_esp, 1024 * 1024);

	device = g_object_new(FU_TYPE_UEFI_CAPSULE_DEVICE, "context", ctx, NULL);
	fu_uefi_capsule_device_set_esp(FU_UEFI_CAPSULE_DEVICE(device), volume_esp);
	firmware =
	    fu_device_prepare_firmware(device, stream, progress, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_null(firmware);
}

static void
fu_uefi_update_esp_no_backup_func(void)
{
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuVolume) volume_esp = fu_volume_new_from_mount_path("/tmp");
	g_autoptr(GBytes) blob = g_bytes_new_static((const guint8 *)"BOB", 3);
	g_autoptr(GBytes) blob_padded = fu_bytes_pad(blob, 4 * 1024 * 1024, 0xFF);
	g_autoptr(GError) error = NULL;
	g_autoptr(GInputStream) stream = g_memory_input_stream_new_from_bytes(blob_padded);

	/* enough to fit the firmware */
	fu_volume_set_filesystem_free(volume_esp, 6 * 1024 * 1024);

	device = g_object_new(FU_TYPE_UEFI_CAPSULE_DEVICE, "context", ctx, NULL);
	fu_device_add_private_flag(device, "no-esp-backup");
	fu_uefi_capsule_device_set_esp(FU_UEFI_CAPSULE_DEVICE(device), volume_esp);
	firmware =
	    fu_device_prepare_firmware(device, stream, progress, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(firmware);
}

static void
fu_uefi_bgrt_func(void)
{
	gboolean ret;
	g_autoptr(GError) error = NULL;
	g_autoptr(FuUefiBgrt) bgrt = fu_uefi_bgrt_new();
	ret = fu_uefi_bgrt_setup(bgrt, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_true(fu_uefi_bgrt_get_supported(bgrt));
	g_assert_cmpint(fu_uefi_bgrt_get_xoffset(bgrt), ==, 123);
	g_assert_cmpint(fu_uefi_bgrt_get_yoffset(bgrt), ==, 456);
	g_assert_cmpint(fu_uefi_bgrt_get_width(bgrt), ==, 54);
	g_assert_cmpint(fu_uefi_bgrt_get_height(bgrt), ==, 24);
}

static void
fu_uefi_framebuffer_func(void)
{
	gboolean ret;
	guint32 height = 0;
	guint32 width = 0;
	g_autoptr(GError) error = NULL;
	ret = fu_uefi_get_framebuffer_size(&width, &height, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(width, ==, 456);
	g_assert_cmpint(height, ==, 789);
}

static void
fu_uefi_bitmap_func(void)
{
	gboolean ret;
	g_autofree gchar *fn = NULL;
	g_autoptr(FuBitmapImage) bmp_image = fu_bitmap_image_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(GInputStream) stream = NULL;

	fn = g_test_build_filename(G_TEST_DIST, "tests", "test.bmp", NULL);
	stream = fu_input_stream_from_path(fn, &error);
	g_assert_no_error(error);
	g_assert_nonnull(stream);
	ret = fu_firmware_parse_stream(FU_FIRMWARE(bmp_image),
				       stream,
				       0x0,
				       FWUPD_INSTALL_FLAG_NONE,
				       &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(fu_bitmap_image_get_width(bmp_image), ==, 54);
	g_assert_cmpint(fu_bitmap_image_get_height(bmp_image), ==, 24);
}

static GBytes *
fu_uefi_cod_device_build_efi_result(const gchar *guidstr)
{
	fwupd_guid_t guid = {0x0};
	gboolean ret;
	guint8 timestamp[16] = {0x0};
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GError) error = NULL;

	fu_byte_array_append_uint32(buf, 0x3A, G_LITTLE_ENDIAN); /* VariableTotalSize */
	fu_byte_array_append_uint32(buf, 0xFF, G_LITTLE_ENDIAN); /* Reserved */
	ret = fwupd_guid_from_string(guidstr, &guid, FWUPD_GUID_FLAG_MIXED_ENDIAN, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_byte_array_append(buf, guid, sizeof(guid));		/* CapsuleGuid */
	g_byte_array_append(buf, timestamp, sizeof(timestamp)); /* CapsuleProcessed */
	fu_byte_array_append_uint32(buf,
				    FU_UEFI_CAPSULE_DEVICE_STATUS_ERROR_PWR_EVT_BATT,
				    G_LITTLE_ENDIAN); /* Status */
	return g_bytes_new(buf->data, buf->len);
}

static void
fu_uefi_cod_device_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuDevice) dev = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *str = NULL;
	FuEfivars *efivars = fu_context_get_efivars(ctx);

	g_autoptr(GBytes) cap0 = NULL;
	g_autoptr(GBytes) cap1 = NULL;
	g_autoptr(GBytes) last = NULL;
	g_autoptr(GBytes) max = NULL;

	last = fu_utf8_to_utf16_bytes("Capsule0001",
				      G_LITTLE_ENDIAN,
				      FU_UTF_CONVERT_FLAG_NONE,
				      &error);
	g_assert_no_error(error);
	g_assert_nonnull(last);
	ret = fu_efivars_set_data_bytes(efivars,
					FU_EFIVARS_GUID_EFI_CAPSULE_REPORT,
					"CapsuleLast",
					last,
					0,
					&error);
	g_assert_no_error(error);
	g_assert_true(ret);

	max = fu_utf8_to_utf16_bytes("Capsule9999",
				     G_LITTLE_ENDIAN,
				     FU_UTF_CONVERT_FLAG_NONE,
				     &error);
	g_assert_no_error(error);
	g_assert_nonnull(last);
	ret = fu_efivars_set_data_bytes(efivars,
					FU_EFIVARS_GUID_EFI_CAPSULE_REPORT,
					"CapsuleMax",
					max,
					0,
					&error);
	g_assert_no_error(error);
	g_assert_true(ret);
	cap0 = fu_uefi_cod_device_build_efi_result("99999999-bf9d-540b-b92b-172ce31013c1");
	ret = fu_efivars_set_data_bytes(efivars,
					FU_EFIVARS_GUID_EFI_CAPSULE_REPORT,
					"Capsule0000",
					cap0,
					0,
					&error);
	g_assert_no_error(error);
	g_assert_true(ret);
	cap1 = fu_uefi_cod_device_build_efi_result("cc4cbfa9-bf9d-540b-b92b-172ce31013c1");
	ret = fu_efivars_set_data_bytes(efivars,
					FU_EFIVARS_GUID_EFI_CAPSULE_REPORT,
					"Capsule0001",
					cap1,
					0,
					&error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* create device */
	dev = g_object_new(FU_TYPE_UEFI_COD_DEVICE,
			   "context",
			   ctx,
			   "fw-class",
			   "cc4cbfa9-bf9d-540b-b92b-172ce31013c1",
			   NULL);
	ret = fu_device_get_results(dev, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* debug */
	str = fu_device_to_string(dev);
	g_debug("%s", str);
	g_assert_cmpint(fu_device_get_update_state(dev), ==, FWUPD_UPDATE_STATE_FAILED_TRANSIENT);
	g_assert_cmpstr(fu_device_get_update_error(dev),
			==,
			"failed to update to 0: error-pwr-evt-batt");
	g_assert_cmpint(fu_uefi_capsule_device_get_status(FU_UEFI_CAPSULE_DEVICE(dev)),
			==,
			FU_UEFI_CAPSULE_DEVICE_STATUS_ERROR_PWR_EVT_BATT);
}

static void
fu_uefi_plugin_func(void)
{
	FuUefiCapsuleDevice *dev;
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuBackend) backend = fu_uefi_capsule_backend_new(ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) devices = NULL;

#ifndef __linux__
	g_test_skip("ESRT data is mocked only on Linux");
	return;
#endif

	/* do not save silo */
	ret = fu_context_load_quirks(ctx, FU_QUIRKS_LOAD_FLAG_NO_CACHE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* add each device */
	ret = fu_backend_coldplug(backend, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	devices = fu_backend_get_devices(backend);
	g_assert_cmpint(devices->len, ==, 3);

	/* system firmware */
	dev = g_ptr_array_index(devices, 0);
	ret = fu_device_probe(FU_DEVICE(dev), &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(fu_uefi_capsule_device_get_kind(dev),
			==,
			FU_UEFI_CAPSULE_DEVICE_KIND_SYSTEM_FIRMWARE);
	g_assert_cmpstr(fu_uefi_capsule_device_get_guid(dev),
			==,
			"ddc0ee61-e7f0-4e7d-acc5-c070a398838e");
	g_assert_cmpint(fu_uefi_capsule_device_get_hardware_instance(dev), ==, 0x0);
	g_assert_cmpint(fu_uefi_capsule_device_get_version(dev), ==, 65586);
	g_assert_cmpint(fu_uefi_capsule_device_get_version_lowest(dev), ==, 65582);
	g_assert_cmpint(fu_uefi_capsule_device_get_version_error(dev), ==, 18472960);
	g_assert_cmpint(fu_uefi_capsule_device_get_capsule_flags(dev), ==, 0xfe);
	g_assert_cmpint(fu_uefi_capsule_device_get_status(dev),
			==,
			FU_UEFI_CAPSULE_DEVICE_STATUS_ERROR_UNSUCCESSFUL);

	/* system firmware */
	dev = g_ptr_array_index(devices, 1);
	ret = fu_device_probe(FU_DEVICE(dev), &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(fu_uefi_capsule_device_get_kind(dev),
			==,
			FU_UEFI_CAPSULE_DEVICE_KIND_DEVICE_FIRMWARE);
	g_assert_cmpstr(fu_uefi_capsule_device_get_guid(dev),
			==,
			"671d19d0-d43c-4852-98d9-1ce16f9967e4");
	g_assert_cmpint(fu_uefi_capsule_device_get_version(dev), ==, 3090287969);
	g_assert_cmpint(fu_uefi_capsule_device_get_version_lowest(dev), ==, 1);
	g_assert_cmpint(fu_uefi_capsule_device_get_version_error(dev), ==, 0);
	g_assert_cmpint(fu_uefi_capsule_device_get_capsule_flags(dev), ==, 32784);
	g_assert_cmpint(fu_uefi_capsule_device_get_status(dev),
			==,
			FU_UEFI_CAPSULE_DEVICE_STATUS_SUCCESS);

	/* invalid */
	dev = g_ptr_array_index(devices, 2);
	ret = fu_device_probe(FU_DEVICE(dev), &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_false(ret);
}

static void
fu_uefi_update_info_func(void)
{
	FuUefiCapsuleDevice *dev;
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuBackend) backend = fu_uefi_capsule_backend_new(ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuUefiUpdateInfo) info2 = fu_uefi_update_info_new();
	g_autoptr(FuUefiUpdateInfo) info = NULL;
	g_autoptr(GBytes) info2_blob = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	FuEfivars *efivars = fu_context_get_efivars(ctx);

	/* create some fake data */
	fu_uefi_update_info_set_guid(info2, "697bd920-12cf-4da9-8385-996909bc6559");
	fu_uefi_update_info_set_capsule_fn(
	    info2,
	    "/EFI/fedora/fw/fwupd-697bd920-12cf-4da9-8385-996909bc6559.cap");
	fu_uefi_update_info_set_hw_inst(info2, 0);
	fu_uefi_update_info_set_capsule_flags(info2, 0x50000);
	fu_uefi_update_info_set_status(info2, FU_UEFI_UPDATE_INFO_STATUS_ATTEMPT_UPDATE);
	info2_blob = fu_firmware_write(FU_FIRMWARE(info2), &error);
	g_assert_no_error(error);
	g_assert_nonnull(info2_blob);
	ret = fu_efivars_set_data_bytes(efivars,
					FU_EFIVARS_GUID_FWUPDATE,
					"fwupd-ddc0ee61-e7f0-4e7d-acc5-c070a398838e-0",
					info2_blob,
					0,
					&error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* add each device */
	ret = fu_backend_coldplug(backend, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	devices = fu_backend_get_devices(backend);
	g_assert_cmpint(devices->len, ==, 3);
	dev = g_ptr_array_index(devices, 0);
	g_assert_cmpint(fu_uefi_capsule_device_get_kind(dev),
			==,
			FU_UEFI_CAPSULE_DEVICE_KIND_SYSTEM_FIRMWARE);
	g_assert_cmpstr(fu_uefi_capsule_device_get_guid(dev),
			==,
			"ddc0ee61-e7f0-4e7d-acc5-c070a398838e");
	info = fu_uefi_capsule_device_load_update_info(dev, &error);
	g_assert_no_error(error);
	g_assert_nonnull(info);
	g_assert_cmpint(fu_firmware_get_version_raw(FU_FIRMWARE(info)), ==, 0x7);
	g_assert_cmpstr(fu_uefi_update_info_get_guid(info),
			==,
			"697bd920-12cf-4da9-8385-996909bc6559");
	g_assert_cmpint(fu_uefi_update_info_get_capsule_flags(info), ==, 0x50000);
	g_assert_cmpint(fu_uefi_update_info_get_hw_inst(info), ==, 0x0);
	g_assert_cmpint(fu_uefi_update_info_get_status(info),
			==,
			FU_UEFI_UPDATE_INFO_STATUS_ATTEMPT_UPDATE);
	g_assert_cmpstr(fu_uefi_update_info_get_capsule_fn(info),
			==,
			"/EFI/fedora/fw/fwupd-697bd920-12cf-4da9-8385-996909bc6559.cap");
}

int
main(int argc, char **argv)
{
	g_autofree gchar *testdatadir = NULL;

	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);

	testdatadir = g_test_build_filename(G_TEST_DIST, "tests", NULL);
	(void)g_setenv("FWUPD_SYSFSFWDIR", testdatadir, TRUE);
	(void)g_setenv("FWUPD_EFIVARS", "dummy", TRUE);
	(void)g_setenv("FWUPD_SYSFSDRIVERDIR", testdatadir, TRUE);
	(void)g_setenv("FWUPD_SYSFSFWATTRIBDIR", testdatadir, TRUE);
	(void)g_setenv("FWUPD_UEFI_TEST", "1", TRUE);

	/* only critical and error are fatal */
	g_log_set_fatal_mask(NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);
	(void)g_setenv("G_MESSAGES_DEBUG", "all", TRUE);

	/* tests go here */
	g_test_add_func("/uefi/update-esp-valid", fu_uefi_update_esp_valid_func);
	g_test_add_func("/uefi/update-esp-invalid", fu_uefi_update_esp_invalid_func);
	g_test_add_func("/uefi/update-esp-no-backup", fu_uefi_update_esp_no_backup_func);
	g_test_add_func("/uefi/bgrt", fu_uefi_bgrt_func);
	g_test_add_func("/uefi/framebuffer", fu_uefi_framebuffer_func);
	g_test_add_func("/uefi/bitmap", fu_uefi_bitmap_func);
	g_test_add_func("/uefi/cod-device", fu_uefi_cod_device_func);
	g_test_add_func("/uefi/update-info", fu_uefi_update_info_func);
	g_test_add_func("/uefi/plugin", fu_uefi_plugin_func);
	return g_test_run();
}
