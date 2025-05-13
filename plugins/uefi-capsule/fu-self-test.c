/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-bitmap-image.h"
#include "fu-context-private.h"
#include "fu-efivars-private.h"
#include "fu-plugin-private.h"
#include "fu-uefi-bgrt.h"
#include "fu-uefi-capsule-backend.h"
#include "fu-uefi-capsule-plugin.h"
#include "fu-uefi-cod-device.h"
#include "fu-uefi-common.h"
#include "fu-uefi-grub-device.h"
#include "fu-uefi-nvram-device.h"
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
	firmware = fu_device_prepare_firmware(device,
					      stream,
					      progress,
					      FU_FIRMWARE_PARSE_FLAG_NONE,
					      &error);
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
	firmware = fu_device_prepare_firmware(device,
					      stream,
					      progress,
					      FU_FIRMWARE_PARSE_FLAG_CACHE_STREAM,
					      &error);
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
	firmware = fu_device_prepare_firmware(device,
					      stream,
					      progress,
					      FU_FIRMWARE_PARSE_FLAG_NONE,
					      &error);
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
	g_assert_cmpint(width, ==, 800);
	g_assert_cmpint(height, ==, 600);
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
				       FU_FIRMWARE_PARSE_FLAG_NONE,
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

static FuVolume *
fu_uefi_plugin_fake_esp_new(void)
{
	g_autofree gchar *tmpdir_efi = NULL;
	g_autofree gchar *tmpdir = NULL;
	g_autoptr(FuVolume) esp = NULL;
	g_autoptr(GError) error = NULL;

	/* enough to fit the firmware */
	tmpdir = g_dir_make_tmp("fwupd-esp-XXXXXX", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);
	esp = fu_volume_new_from_mount_path(tmpdir);
	fu_volume_set_filesystem_free(esp, 10 * 1024 * 1024);
	fu_volume_set_partition_kind(esp, FU_VOLUME_KIND_ESP);
	fu_volume_set_partition_uuid(esp, "00000000-0000-0000-0000-000000000000");

	/* make fu_uefi_get_esp_path_for_os() distro-neutral */
	tmpdir_efi = g_build_filename(tmpdir, "EFI", "systemd", NULL);
	g_mkdir_with_parents(tmpdir_efi, 0700);

	/* success */
	return g_steal_pointer(&esp);
}

static void
fu_uefi_plugin_no_coalesce_func(void)
{
	FuUefiCapsuleDevice *dev1;
	FuUefiCapsuleDevice *dev2;
	GPtrArray *devices;
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuVolume) esp = fu_uefi_plugin_fake_esp_new();
	g_autoptr(GError) error = NULL;

#ifndef __linux__
	g_test_skip("ESRT data is mocked only on Linux");
	return;
#endif

	/* override ESP */
	fu_context_add_esp_volume(ctx, esp);

	/* set up at least one HWID */
	fu_config_set_default(fu_context_get_config(ctx), "fwupd", "Manufacturer", "fwupd");

	/* load dummy hwids */
	ret = fu_context_load_hwinfo(ctx, progress, FU_CONTEXT_HWID_FLAG_LOAD_CONFIG, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* create plugin, and ->startup then ->coldplug */
	plugin =
	    g_object_new(FU_TYPE_UEFI_CAPSULE_PLUGIN, "context", ctx, "name", "uefi_capsule", NULL);
	ret = fu_plugin_runner_startup(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_plugin_runner_coldplug(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* do not save silo */
	ret = fu_context_load_quirks(ctx, FU_QUIRKS_LOAD_FLAG_NO_CACHE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* check each device */
	devices = fu_plugin_get_devices(plugin);
	g_assert_cmpint(devices->len, ==, 2);

	/* system firmware */
	dev1 = g_ptr_array_index(devices, 0);
	g_assert_cmpint(fu_uefi_capsule_device_get_kind(dev1),
			==,
			FU_UEFI_CAPSULE_DEVICE_KIND_SYSTEM_FIRMWARE);
	g_assert_cmpstr(fu_uefi_capsule_device_get_guid(dev1),
			==,
			"ddc0ee61-e7f0-4e7d-acc5-c070a398838e");
	g_assert_cmpint(fu_uefi_capsule_device_get_hardware_instance(dev1), ==, 0x0);
	g_assert_cmpint(fu_uefi_capsule_device_get_version(dev1), ==, 65586);
	g_assert_cmpint(fu_uefi_capsule_device_get_version_lowest(dev1), ==, 65582);
	g_assert_cmpint(fu_uefi_capsule_device_get_version_error(dev1), ==, 18472960);
	g_assert_cmpint(fu_uefi_capsule_device_get_capsule_flags(dev1), ==, 0xfe);
	g_assert_cmpint(fu_uefi_capsule_device_get_status(dev1),
			==,
			FU_UEFI_CAPSULE_DEVICE_STATUS_ERROR_UNSUCCESSFUL);
	g_assert_true(fu_device_has_flag(FU_DEVICE(dev1), FWUPD_DEVICE_FLAG_UPDATABLE));

	/* system firmware */
	dev2 = g_ptr_array_index(devices, 1);
	g_assert_cmpint(fu_uefi_capsule_device_get_kind(dev2),
			==,
			FU_UEFI_CAPSULE_DEVICE_KIND_DEVICE_FIRMWARE);
	g_assert_cmpstr(fu_uefi_capsule_device_get_guid(dev2),
			==,
			"671d19d0-d43c-4852-98d9-1ce16f9967e4");
	g_assert_cmpint(fu_uefi_capsule_device_get_version(dev2), ==, 3090287969);
	g_assert_cmpint(fu_uefi_capsule_device_get_version_lowest(dev2), ==, 1);
	g_assert_cmpint(fu_uefi_capsule_device_get_version_error(dev2), ==, 0);
	g_assert_cmpint(fu_uefi_capsule_device_get_capsule_flags(dev2), ==, 32784);
	g_assert_cmpint(fu_uefi_capsule_device_get_status(dev2),
			==,
			FU_UEFI_CAPSULE_DEVICE_STATUS_SUCCESS);
	g_assert_true(fu_device_has_flag(FU_DEVICE(dev2), FWUPD_DEVICE_FLAG_UPDATABLE));

	/* ensure the other device is not updatable when the first is updated */
	fu_device_set_update_state(FU_DEVICE(dev2), FWUPD_UPDATE_STATE_NEEDS_REBOOT);
	g_assert_false(fu_device_has_flag(FU_DEVICE(dev1), FWUPD_DEVICE_FLAG_UPDATABLE));
}

static void
fu_uefi_plugin_no_flashes_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuFirmware) firmware = fu_firmware_new();
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuVolume) esp = fu_uefi_plugin_fake_esp_new();
	g_autoptr(GBytes) blob = g_bytes_new_static("GUIDGUIDGUIDGUID", 16);
	g_autoptr(GError) error = NULL;

	/* override ESP */
	fu_context_add_esp_volume(ctx, esp);

	/* load dummy hwids */
	ret = fu_context_load_hwinfo(ctx, progress, FU_CONTEXT_HWID_FLAG_LOAD_CONFIG, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* create plugin, and ->startup then ->coldplug */
	plugin =
	    g_object_new(FU_TYPE_UEFI_CAPSULE_PLUGIN, "context", ctx, "name", "uefi_capsule", NULL);
	ret = fu_plugin_runner_startup(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_plugin_runner_coldplug(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* test with almost no flashes left */
	device = g_object_new(FU_TYPE_UEFI_NVRAM_DEVICE,
			      "context",
			      ctx,
			      "fw-class",
			      "cc4cbfa9-bf9d-540b-b92b-172ce31013c1",
			      NULL);
	fu_device_add_private_flag(device, FU_UEFI_CAPSULE_DEVICE_FLAG_NO_UX_CAPSULE);
	fu_device_set_flashes_left(device, 2);
	ret = fu_plugin_runner_write_firmware(plugin,
					      device,
					      firmware,
					      progress,
					      FWUPD_INSTALL_FLAG_NONE,
					      &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_false(ret);
}

static gboolean
fu_uefi_plugin_esp_file_exists(FuVolume *esp, const gchar *filename)
{
	g_autofree gchar *mount_point = fu_volume_get_mount_point(esp);
	g_autofree gchar *fn = g_build_filename(mount_point, filename, NULL);
	return g_file_test(fn, G_FILE_TEST_EXISTS);
}

static void
fu_uefi_plugin_esp_rmtree(FuVolume *esp)
{
	gboolean ret;
	g_autofree gchar *mount_point = fu_volume_get_mount_point(esp);
	g_autoptr(GError) error = NULL;
	ret = fu_path_rmtree(mount_point, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_uefi_plugin_nvram_func(void)
{
	gboolean ret;
	guint16 bootnext = 0;
	guint16 idx;
	g_autoptr(GBytes) blob = g_bytes_new_static("GUIDGUIDGUIDGUID", 16);
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuEfiLoadOption) loadopt = fu_efi_load_option_new();
	g_autoptr(FuFirmware) firmware = fu_firmware_new_from_bytes(blob);
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuVolume) esp = fu_uefi_plugin_fake_esp_new();
	g_autoptr(GArray) bootorder = NULL;
	g_autoptr(GError) error = NULL;

#ifndef __x86_64__
	g_test_skip("NVRAM binary is mocked only for x86_64");
	return;
#endif

	/* override ESP */
	fu_context_add_esp_volume(ctx, esp);

	/* set up system so that secure boot is on */
	ret = fu_efivars_set_secure_boot(fu_context_get_efivars(ctx), TRUE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_efivars_create_boot_entry_for_volume(fu_context_get_efivars(ctx),
						      0x0000,
						      esp,
						      "Fedora",
						      "grubx64.efi",
						      &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_efivars_set_boot_current(fu_context_get_efivars(ctx), 0x0000, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_efivars_build_boot_order(fu_context_get_efivars(ctx), &error, 0x0000, G_MAXUINT16);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* load dummy hwids */
	ret = fu_context_load_hwinfo(ctx, progress, FU_CONTEXT_HWID_FLAG_LOAD_CONFIG, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* create plugin, and ->startup then ->coldplug */
	plugin =
	    g_object_new(FU_TYPE_UEFI_CAPSULE_PLUGIN, "context", ctx, "name", "uefi_capsule", NULL);
	fu_plugin_set_config_default(plugin, "ScreenWidth", "800");
	fu_plugin_set_config_default(plugin, "ScreenHeight", "600");
	ret = fu_plugin_runner_startup(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_plugin_runner_coldplug(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* test with a dummy device that just writes the splash */
	device = g_object_new(FU_TYPE_UEFI_NVRAM_DEVICE,
			      "context",
			      ctx,
			      "fw-class",
			      "cc4cbfa9-bf9d-540b-b92b-172ce31013c1",
			      NULL);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_private_flag(device, FU_UEFI_CAPSULE_DEVICE_FLAG_USE_FWUPD_EFI);
	fu_device_add_private_flag(device, FU_UEFI_CAPSULE_DEVICE_FLAG_USE_LEGACY_BOOTMGR_DESC);
	fu_device_add_private_flag(device, FU_UEFI_CAPSULE_DEVICE_FLAG_MODIFY_BOOTORDER);
	fu_uefi_capsule_device_set_esp(FU_UEFI_CAPSULE_DEVICE(device), esp);
	ret = fu_device_prepare(device, progress, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_plugin_runner_write_firmware(plugin,
					      device,
					      firmware,
					      progress,
					      FWUPD_INSTALL_FLAG_NONE,
					      &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* check UX splash was created */
	g_assert_true(fu_uefi_plugin_esp_file_exists(
	    esp,
	    "EFI/systemd/fw/fwupd-3b8c8162-188c-46a4-aec9-be43f1d65697.cap"));
	g_assert_true(fu_efivars_exists(fu_context_get_efivars(ctx),
					FU_EFIVARS_GUID_FWUPDATE,
					"fwupd-ux-capsule"));

	/* check FW was created */
	g_assert_true(fu_uefi_plugin_esp_file_exists(
	    esp,
	    "EFI/systemd/fw/fwupd-cc4cbfa9-bf9d-540b-b92b-172ce31013c1.cap"));
	g_assert_true(fu_efivars_exists(fu_context_get_efivars(ctx),
					FU_EFIVARS_GUID_FWUPDATE,
					"fwupd-ux-capsule"));
	g_assert_true(fu_efivars_exists(fu_context_get_efivars(ctx),
					FU_EFIVARS_GUID_FWUPDATE,
					"fwupd-cc4cbfa9-bf9d-540b-b92b-172ce31013c1-0"));

	/* we skipped this, so emulate something */
	g_assert_no_error(error);
	g_assert_true(ret);

	/* verify BootOrder */
	bootorder = fu_efivars_get_boot_order(fu_context_get_efivars(ctx), &error);
	g_assert_no_error(error);
	g_assert_nonnull(bootorder);
	g_assert_cmpint(bootorder->len, ==, 2);
	idx = g_array_index(bootorder, guint16, 0);
	g_assert_cmpint(idx, ==, 0x0000);
	idx = g_array_index(bootorder, guint16, 1);
	g_assert_cmpint(idx, ==, 0x0001);

	/* verify BootNext */
	ret = fu_efivars_get_boot_next(fu_context_get_efivars(ctx), &bootnext, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(bootnext, ==, 0x0001);

	/* clear results */
	ret = fu_plugin_runner_clear_results(plugin, device, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* cleanup */
	ret = fu_plugin_runner_reboot_cleanup(plugin, device, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* check both files and variables no longer exist */
	g_assert_false(fu_uefi_plugin_esp_file_exists(
	    esp,
	    "EFI/systemd/fw/fwupd-3b8c8162-188c-46a4-aec9-be43f1d65697.cap"));
	g_assert_false(fu_efivars_exists(fu_context_get_efivars(ctx),
					 FU_EFIVARS_GUID_FWUPDATE,
					 "fwupd-ux-capsule"));
	g_assert_false(fu_uefi_plugin_esp_file_exists(
	    esp,
	    "EFI/systemd/fw/fwupd-cc4cbfa9-bf9d-540b-b92b-172ce31013c1.cap"));
	g_assert_false(fu_efivars_exists(fu_context_get_efivars(ctx),
					 FU_EFIVARS_GUID_FWUPDATE,
					 "fwupd-cc4cbfa9-bf9d-540b-b92b-172ce31013c1-0"));

	/* check BootNext was removed */
	g_assert_false(
	    fu_efivars_exists(fu_context_get_efivars(ctx), FU_EFIVARS_GUID_EFI_GLOBAL, "BootNext"));

	/* get results */
	ret = fu_device_get_results(device, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* cleanup */
	fu_uefi_plugin_esp_rmtree(esp);
}

static void
fu_uefi_plugin_cod_func(void)
{
	gboolean ret;
	g_autoptr(GBytes) blob = g_bytes_new_static("GUIDGUIDGUIDGUID", 16);
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuEfiLoadOption) loadopt = fu_efi_load_option_new();
	g_autoptr(FuFirmware) firmware = fu_firmware_new_from_bytes(blob);
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuVolume) esp = fu_uefi_plugin_fake_esp_new();
	g_autoptr(GByteArray) buf_last = NULL;
	g_autoptr(GError) error = NULL;

	/* override ESP */
	fu_context_add_esp_volume(ctx, esp);

	/* set up system  */
	buf_last = fu_utf8_to_utf16_byte_array("Capsule0001",
					       G_LITTLE_ENDIAN,
					       FU_UTF_CONVERT_FLAG_NONE,
					       &error);
	g_assert_no_error(error);
	g_assert_nonnull(buf_last);
	ret = fu_efivars_set_data(fu_context_get_efivars(ctx),
				  FU_EFIVARS_GUID_EFI_CAPSULE_REPORT,
				  "CapsuleLast",
				  buf_last->data,
				  buf_last->len,
				  0x0,
				  &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* load dummy hwids */
	ret = fu_context_load_hwinfo(ctx, progress, FU_CONTEXT_HWID_FLAG_LOAD_CONFIG, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* create plugin, and ->startup then ->coldplug */
	plugin =
	    g_object_new(FU_TYPE_UEFI_CAPSULE_PLUGIN, "context", ctx, "name", "uefi_capsule", NULL);
	ret = fu_plugin_runner_startup(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_plugin_runner_coldplug(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* test with a dummy device that just writes the splash */
	device = g_object_new(FU_TYPE_UEFI_COD_DEVICE,
			      "context",
			      ctx,
			      "fw-class",
			      "cc4cbfa9-bf9d-540b-b92b-172ce31013c1",
			      NULL);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_private_flag(device, FU_UEFI_CAPSULE_DEVICE_FLAG_NO_UX_CAPSULE);
	fu_uefi_capsule_device_set_esp(FU_UEFI_CAPSULE_DEVICE(device), esp);

	/* write default capsule */
	ret = fu_plugin_runner_write_firmware(plugin,
					      device,
					      firmware,
					      progress,
					      FWUPD_INSTALL_FLAG_NONE,
					      &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_true(fu_uefi_plugin_esp_file_exists(
	    esp,
	    "EFI/UpdateCapsule/fwupd-cc4cbfa9-bf9d-540b-b92b-172ce31013c1.cap"));

	/* try again with a different filename */
	fu_device_add_private_flag(device, FU_UEFI_CAPSULE_DEVICE_FLAG_COD_DELL_RECOVERY);
	ret = fu_plugin_runner_write_firmware(plugin,
					      device,
					      firmware,
					      progress,
					      FWUPD_INSTALL_FLAG_NONE,
					      &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_true(fu_uefi_plugin_esp_file_exists(esp, "EFI/dell/bios/recovery/BIOS_TRS.rcv"));

	/* try again with a different filename */
	fu_device_add_private_flag(device, FU_UEFI_CAPSULE_DEVICE_FLAG_COD_INDEXED_FILENAME);
	ret = fu_plugin_runner_write_firmware(plugin,
					      device,
					      firmware,
					      progress,
					      FWUPD_INSTALL_FLAG_NONE,
					      &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_true(
	    fu_uefi_plugin_esp_file_exists(esp, "EFI/UpdateCapsule/CapsuleUpdateFile0000.bin"));

	/* get results */
	ret = fu_device_get_results(device, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* cleanup */
	fu_uefi_plugin_esp_rmtree(esp);
}

static void
fu_uefi_plugin_grub_func(void)
{
	gboolean ret;
	g_autoptr(GBytes) blob = g_bytes_new_static("GUIDGUIDGUIDGUID", 16);
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuEfiLoadOption) loadopt = fu_efi_load_option_new();
	g_autoptr(FuFirmware) firmware = fu_firmware_new_from_bytes(blob);
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuVolume) esp = fu_uefi_plugin_fake_esp_new();
	g_autoptr(GByteArray) buf_last = NULL;
	g_autoptr(GError) error = NULL;

#ifndef __x86_64__
	g_test_skip("ESRT is mocked only for x86_64");
	return;
#endif

	/* set up system so that secure boot is on */
	ret = fu_efivars_set_secure_boot(fu_context_get_efivars(ctx), TRUE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* load dummy hwids */
	ret = fu_context_load_hwinfo(ctx, progress, FU_CONTEXT_HWID_FLAG_LOAD_CONFIG, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* override ESP */
	fu_context_add_esp_volume(ctx, esp);

	/* create plugin, and ->startup then ->coldplug */
	plugin =
	    g_object_new(FU_TYPE_UEFI_CAPSULE_PLUGIN, "context", ctx, "name", "uefi_capsule", NULL);
	ret = fu_plugin_runner_startup(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_plugin_runner_coldplug(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* test with a dummy device */
	device = g_object_new(FU_TYPE_UEFI_GRUB_DEVICE,
			      "context",
			      ctx,
			      "fw-class",
			      "cc4cbfa9-bf9d-540b-b92b-172ce31013c1",
			      NULL);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_private_flag(device, FU_UEFI_CAPSULE_DEVICE_FLAG_NO_UX_CAPSULE);
	fu_uefi_capsule_device_set_esp(FU_UEFI_CAPSULE_DEVICE(device), esp);

	/* write */
	ret = fu_device_prepare(device, progress, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_plugin_runner_write_firmware(plugin,
					      device,
					      firmware,
					      progress,
					      FWUPD_INSTALL_FLAG_NONE,
					      &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_true(fu_uefi_plugin_esp_file_exists(
	    esp,
	    "EFI/systemd/fw/fwupd-cc4cbfa9-bf9d-540b-b92b-172ce31013c1.cap"));

	/* cleanup */
	fu_uefi_plugin_esp_rmtree(esp);
}

static void
fu_uefi_update_info_xml_func(void)
{
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autofree gchar *csum1 = NULL;
	g_autofree gchar *csum2 = NULL;
	g_autofree gchar *xml_out = NULL;
	g_autofree gchar *xml_src = NULL;
	g_autoptr(FuFirmware) firmware1 = FU_FIRMWARE(fu_uefi_update_info_new());
	g_autoptr(FuFirmware) firmware2 = FU_FIRMWARE(fu_uefi_update_info_new());
	g_autoptr(FuFirmware) firmware3 = FU_FIRMWARE(fu_uefi_update_info_new());
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GError) error = NULL;

	/* build and write */
	filename =
	    g_test_build_filename(G_TEST_DIST, "tests", "uefi-update-info.builder.xml", NULL);
	ret = g_file_get_contents(filename, &xml_src, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_firmware_build_from_xml(firmware1, xml_src, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	fw = fu_firmware_write(firmware1, &error);
	g_assert_no_error(error);
	g_assert_nonnull(fw);
	csum1 = fu_firmware_get_checksum(firmware1, G_CHECKSUM_SHA1, &error);
	g_assert_no_error(error);
	g_assert_cmpstr(csum1, ==, "18e8c43a912d3918498723340ae80a57d8b0657c");

	/* ensure we can parse */
	ret = fu_firmware_parse_bytes(firmware3, fw, 0x0, FU_FIRMWARE_PARSE_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* ensure we can round-trip */
	xml_out = fu_firmware_export_to_xml(firmware1, FU_FIRMWARE_EXPORT_FLAG_NONE, &error);
	g_assert_no_error(error);
	ret = fu_firmware_build_from_xml(firmware2, xml_out, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	csum2 = fu_firmware_get_checksum(firmware2, G_CHECKSUM_SHA1, &error);
	g_assert_cmpstr(csum1, ==, csum2);
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
	g_autofree gchar *testdatadir_mut = NULL;

	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);

	testdatadir = g_test_build_filename(G_TEST_DIST, "tests", NULL);
	testdatadir_mut = g_test_build_filename(G_TEST_BUILT, "tests", NULL);
	(void)g_setenv("FWUPD_SYSFSFWDIR", testdatadir, TRUE);
	(void)g_setenv("FWUPD_EFIVARS", "dummy", TRUE);
	(void)g_setenv("FWUPD_SYSFSDRIVERDIR", testdatadir, TRUE);
	(void)g_setenv("FWUPD_SYSFSFWATTRIBDIR", testdatadir, TRUE);
	(void)g_setenv("FWUPD_DATADIR_QUIRKS", testdatadir, TRUE);
	(void)g_setenv("FWUPD_HOSTFS_BOOT", testdatadir, TRUE);
	(void)g_setenv("FWUPD_EFIAPPDIR", testdatadir_mut, TRUE);
	(void)g_setenv("FWUPD_ACPITABLESDIR", testdatadir_mut, TRUE);
	(void)g_setenv("FWUPD_DATADIR", g_test_get_dir(G_TEST_BUILT), TRUE);
	(void)g_setenv("FWUPD_UEFI_TEST", "1", TRUE);
	(void)g_setenv("LANGUAGE", "en", TRUE);
	(void)g_setenv("PATH", testdatadir, TRUE);

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
	g_test_add_func("/uefi/update-info{xml}", fu_uefi_update_info_xml_func);
	g_test_add_func("/uefi/plugin{no-coalesce}", fu_uefi_plugin_no_coalesce_func);
	g_test_add_func("/uefi/plugin{no-flashes-left}", fu_uefi_plugin_no_flashes_func);
	g_test_add_func("/uefi/plugin{nvram}", fu_uefi_plugin_nvram_func);
	g_test_add_func("/uefi/plugin{cod}", fu_uefi_plugin_cod_func);
	g_test_add_func("/uefi/plugin{grub}", fu_uefi_plugin_grub_func);
	return g_test_run();
}
