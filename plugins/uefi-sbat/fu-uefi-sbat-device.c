/*
 * Copyright 2024 Richard hughes <Richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-uefi-sbat-device.h"
#include "fu-uefi-sbat-firmware.h"

struct _FuUefiSbatDevice {
	FuDevice parent_instance;
};

G_DEFINE_TYPE(FuUefiSbatDevice, fu_uefi_sbat_device, FU_TYPE_DEVICE)

static gboolean
fu_uefi_sbat_device_probe(FuDevice *device, GError **error)
{
	g_autofree gchar *distro_id = NULL;

	distro_id = g_get_os_info(G_OS_INFO_KEY_ID);
	if (distro_id == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no os-release ID");
		return FALSE;
	}
	fu_device_build_vendor_id(device, "OS", distro_id);

	/* try to lookup /etc/os-release ID key */
	fu_device_add_instance_str(device, "OS", distro_id);
	fu_device_add_instance_str(device, "VAR", "SbatLevelRT");
	if (!fu_device_build_instance_id_full(device,
					      FU_DEVICE_INSTANCE_FLAG_QUIRKS,
					      error,
					      "UEFI",
					      "OS",
					      NULL))
		return FALSE;
	if (!fu_device_build_instance_id_full(device,
					      FU_DEVICE_INSTANCE_FLAG_VISIBLE,
					      error,
					      "UEFI",
					      "OS",
					      "VAR",
					      NULL))
		return FALSE;

	/* success */
	return TRUE;
}

static FuFirmware *
fu_uefi_sbat_device_prepare_firmware(FuDevice *device,
				     GInputStream *stream,
				     FuProgress *progress,
				     FwupdInstallFlags flags,
				     GError **error)
{
	FuContext *ctx = fu_device_get_context(device);
	g_autoptr(FuFirmware) firmware_pefile = fu_pefile_firmware_new();
	g_autoptr(FuFirmware) firmware_sbat = fu_uefi_sbat_firmware_new();
	g_autoptr(GInputStream) stream_sbata = NULL;
	g_autoptr(GPtrArray) esp_files = NULL;

	if (!fu_firmware_parse_stream(firmware_pefile, stream, 0x0, flags, error))
		return NULL;

	/* grab .sbata and parse */
	stream_sbata = fu_firmware_get_image_by_id_stream(firmware_pefile, ".sbata", error);
	if (stream_sbata == NULL)
		return NULL;
	if (!fu_firmware_parse_stream(firmware_sbat, stream_sbata, 0x0, flags, error))
		return NULL;

	/* verify there is nothing in the ESP with a lower version */
	esp_files = fu_context_get_esp_files(ctx,
					     FU_CONTEXT_ESP_FILE_FLAG_INCLUDE_FIRST_STAGE |
						 FU_CONTEXT_ESP_FILE_FLAG_INCLUDE_SECOND_STAGE,
					     error);
	if (esp_files == NULL) {
		g_prefix_error(error, "failed to get files on ESP: ");
		return NULL;
	}
	for (guint i = 0; i < esp_files->len; i++) {
		FuFirmware *esp_file = g_ptr_array_index(esp_files, i);
		if (!fu_firmware_check_compatible(firmware_sbat, esp_file, flags, error)) {
			g_prefix_error(error,
				       "SBAT level is too old on %s: ",
				       fu_firmware_get_filename(esp_file));
			return NULL;
		}
	}

	/* success */
	return g_steal_pointer(&firmware_pefile);
}

static gboolean
fu_uefi_sbat_device_write_firmware(FuDevice *device,
				   FuFirmware *firmware,
				   FuProgress *progress,
				   FwupdInstallFlags flags,
				   GError **error)
{
	FuContext *ctx = fu_device_get_context(device);
	FuEfivars *efivars = fu_context_get_efivars(ctx);
	guint16 idx = 0;
	g_autofree gchar *dirname = NULL;
	g_autofree gchar *filename_shim = NULL;
	g_autofree gchar *filename_revocation = NULL;
	g_autofree gchar *fp_name = NULL;
	g_autofree gchar *mount_point = NULL;
	g_autoptr(FuDeviceLocker) volume_locker = NULL;
	g_autoptr(FuEfiLoadOption) entry = NULL;
	g_autoptr(FuFirmware) dp_fp = NULL;
	g_autoptr(FuFirmware) dp_hdd = NULL;
	g_autoptr(FuFirmware) dp_list = NULL;
	g_autoptr(FuVolume) volume = NULL;
	g_autoptr(GBytes) fw = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 86, "mount ESP");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 14, NULL);

	/* get the mountpoint of the currently used ESP */
	if (!fu_efivars_get_boot_current(efivars, &idx, error))
		return FALSE;
	entry = fu_efivars_get_boot_entry(efivars, idx, error);
	if (entry == NULL)
		return FALSE;
	dp_list =
	    fu_firmware_get_image_by_gtype(FU_FIRMWARE(entry), FU_TYPE_EFI_DEVICE_PATH_LIST, error);
	if (dp_list == NULL)
		return FALSE;
	dp_hdd = fu_firmware_get_image_by_gtype(dp_list, FU_TYPE_EFI_HARD_DRIVE_DEVICE_PATH, error);
	if (dp_hdd == NULL)
		return FALSE;
	volume = fu_context_get_esp_volume_by_hard_drive_device_path(
	    ctx,
	    FU_EFI_HARD_DRIVE_DEVICE_PATH(dp_hdd),
	    error);
	if (volume == NULL)
		return FALSE;
	volume_locker = fu_volume_locker(volume, error);
	if (volume_locker == NULL)
		return FALSE;
	mount_point = fu_volume_get_mount_point(volume);
	if (mount_point == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "no mountpoint for %s",
			    fu_volume_get_id(volume));
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* get the location of the CurrentBoot ESP file */
	dp_fp = fu_firmware_get_image_by_gtype(dp_list, FU_TYPE_EFI_FILE_PATH_DEVICE_PATH, error);
	if (dp_fp == NULL)
		return FALSE;
	fp_name = fu_efi_file_path_device_path_get_name(FU_EFI_FILE_PATH_DEVICE_PATH(dp_fp), error);
	if (fp_name == NULL)
		return FALSE;
	filename_shim = g_build_filename(mount_point, fp_name, NULL);
	dirname = g_path_get_dirname(filename_shim);
	filename_revocation = g_build_filename(dirname, "revocations.efi", NULL);

	/* write image */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;
	if (!fu_bytes_set_contents(filename_revocation, fw, error))
		return FALSE;
	g_debug("wrote %s", filename_revocation);
	fu_progress_step_done(progress);

	/* success! */
	return TRUE;
}

static void
fu_uefi_sbat_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static void
fu_uefi_sbat_device_init(FuUefiSbatDevice *self)
{
	fu_device_set_name(FU_DEVICE(self), "SBAT");
	fu_device_set_summary(FU_DEVICE(self), "Generation number based revocation mechanism");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_add_protocol(FU_DEVICE(self), "com.uefi.sbat");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_NEEDS_REBOOT);
	fu_device_set_physical_id(FU_DEVICE(self), "UEFI");
	fu_device_set_logical_id(FU_DEVICE(self), "SBAT");
}

static void
fu_uefi_sbat_device_class_init(FuUefiSbatDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->probe = fu_uefi_sbat_device_probe;
	device_class->prepare_firmware = fu_uefi_sbat_device_prepare_firmware;
	device_class->write_firmware = fu_uefi_sbat_device_write_firmware;
	device_class->set_progress = fu_uefi_sbat_device_set_progress;
}

FuUefiSbatDevice *
fu_uefi_sbat_device_new(FuContext *ctx, GBytes *blob, GError **error)
{
	g_autoptr(FuFirmware) firmware = fu_uefi_sbat_firmware_new();
	g_autoptr(FuUefiSbatDevice) self = NULL;

	g_return_val_if_fail(FU_IS_CONTEXT(ctx), NULL);
	g_return_val_if_fail(blob != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* copy the version across */
	if (!fu_firmware_parse(firmware, blob, FWUPD_INSTALL_FLAG_NONE, error))
		return NULL;
	self = g_object_new(FU_TYPE_UEFI_SBAT_DEVICE, "context", ctx, NULL);
	fu_device_set_version(FU_DEVICE(self), fu_firmware_get_version(firmware));

	/* success */
	return g_steal_pointer(&self);
}
