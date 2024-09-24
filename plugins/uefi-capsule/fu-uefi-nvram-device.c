/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 * Copyright 2018 Mario Limonciello <mario.limonciello@amd.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-uefi-bootmgr.h"
#include "fu-uefi-common.h"
#include "fu-uefi-nvram-device.h"

struct _FuUefiNvramDevice {
	FuUefiDevice parent_instance;
};

G_DEFINE_TYPE(FuUefiNvramDevice, fu_uefi_nvram_device, FU_TYPE_UEFI_DEVICE)

static gboolean
fu_uefi_nvram_device_get_results(FuDevice *device, GError **error)
{
	FuContext *ctx = fu_device_get_context(device);
	FuEfivars *efivars = fu_context_get_efivars(ctx);
	g_autoptr(GError) error_local = NULL;

	/* check if something rudely removed our BOOTXXXX entry */
	if (!fu_uefi_bootmgr_verify_fwupd(efivars, &error_local)) {
		if (fu_device_has_private_flag(device,
					       FU_UEFI_DEVICE_FLAG_SUPPORTS_BOOT_ORDER_LOCK)) {
			g_prefix_error(&error_local,
				       "boot entry missing; "
				       "perhaps 'Boot Order Lock' enabled in the BIOS: ");
			fu_device_set_update_state(device, FWUPD_UPDATE_STATE_FAILED_TRANSIENT);
		} else {
			g_prefix_error(&error_local, "boot entry missing: ");
			fu_device_set_update_state(device, FWUPD_UPDATE_STATE_FAILED);
		}
		fu_device_set_update_error(device, error_local->message);
		return TRUE;
	}

	/* parent */
	return FU_DEVICE_CLASS(fu_uefi_nvram_device_parent_class)->get_results(device, error);
}

static gboolean
fu_uefi_nvram_device_write_firmware(FuDevice *device,
				    FuFirmware *firmware,
				    FuProgress *progress,
				    FwupdInstallFlags flags,
				    GError **error)
{
	FuContext *ctx = fu_device_get_context(device);
	FuEfivars *efivars = fu_context_get_efivars(ctx);
	FuUefiDevice *self = FU_UEFI_DEVICE(device);
	FuUefiBootmgrFlags bootmgr_flags = FU_UEFI_BOOTMGR_FLAG_NONE;
	const gchar *bootmgr_desc = "Linux Firmware Updater";
	const gchar *fw_class = fu_uefi_device_get_guid(self);
	FuVolume *esp = fu_uefi_device_get_esp(self);
	g_autofree gchar *esp_path = fu_volume_get_mount_point(esp);
	g_autoptr(GBytes) fixed_fw = NULL;
	g_autoptr(GBytes) fw = NULL;
	g_autofree gchar *capsule_path = NULL;
	g_autofree gchar *basename = NULL;
	g_autofree gchar *directory = NULL;
	g_autofree gchar *fn = NULL;
	g_autofree gchar *varname = fu_uefi_device_build_varname(self);

	/* ensure we have the existing state */
	if (fw_class == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "cannot update device info with no GUID");
		return FALSE;
	}

	/* get default image */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;

	/* save the blob to the ESP */
	directory = fu_uefi_get_esp_path_for_os(esp_path);
	basename = g_strdup_printf("fwupd-%s.cap", fw_class);
	capsule_path = g_build_filename(directory, "fw", basename, NULL);
	fn = g_build_filename(esp_path, capsule_path, NULL);
	if (!fu_path_mkdir_parent(fn, error))
		return FALSE;
	fixed_fw = fu_uefi_device_fixup_firmware(self, fw, error);
	if (fixed_fw == NULL)
		return FALSE;
	if (!fu_bytes_set_contents(fn, fixed_fw, error))
		return FALSE;

	/* enable debugging in the EFI binary */
	if (!fu_uefi_device_perhaps_enable_debugging(self, error))
		return FALSE;

	/* delete the old log to save space */
	if (fu_efivars_exists(efivars, FU_EFIVARS_GUID_FWUPDATE, "FWUPDATE_DEBUG_LOG")) {
		if (!fu_efivars_delete(efivars,
				       FU_EFIVARS_GUID_FWUPDATE,
				       "FWUPDATE_DEBUG_LOG",
				       error))
			return FALSE;
	}

	/* set the blob header shared with fwupd.efi */
	if (!fu_uefi_device_write_update_info(self, capsule_path, varname, fw_class, error))
		return FALSE;

	/* update the firmware before the bootloader runs */
	if (fu_device_has_private_flag(device, FU_UEFI_DEVICE_FLAG_USE_SHIM_FOR_SB))
		bootmgr_flags |= FU_UEFI_BOOTMGR_FLAG_USE_SHIM_FOR_SB;
	if (fu_device_has_private_flag(device, FU_UEFI_DEVICE_FLAG_USE_SHIM_UNIQUE))
		bootmgr_flags |= FU_UEFI_BOOTMGR_FLAG_USE_SHIM_UNIQUE;
	if (fu_device_has_private_flag(device, FU_UEFI_DEVICE_FLAG_MODIFY_BOOTORDER))
		bootmgr_flags |= FU_UEFI_BOOTMGR_FLAG_MODIFY_BOOTORDER;

	/* some legacy devices use the old name to deduplicate boot entries */
	if (fu_device_has_private_flag(device, FU_UEFI_DEVICE_FLAG_USE_LEGACY_BOOTMGR_DESC))
		bootmgr_desc = "Linux-Firmware-Updater";
	if (!fu_uefi_bootmgr_bootnext(efivars, esp, bootmgr_desc, bootmgr_flags, error))
		return FALSE;

	/* success! */
	return TRUE;
}

static void
fu_uefi_nvram_device_report_metadata_pre(FuDevice *device, GHashTable *metadata)
{
	/* FuUefiDevice */
	FU_DEVICE_CLASS(fu_uefi_nvram_device_parent_class)->report_metadata_pre(device, metadata);
	g_hash_table_insert(metadata, g_strdup("CapsuleApplyMethod"), g_strdup("nvram"));
}

static void
fu_uefi_nvram_device_init(FuUefiNvramDevice *self)
{
	fu_device_set_summary(FU_DEVICE(self),
			      "UEFI System Resource Table device (updated via NVRAM)");
}

static void
fu_uefi_nvram_device_class_init(FuUefiNvramDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->get_results = fu_uefi_nvram_device_get_results;
	device_class->write_firmware = fu_uefi_nvram_device_write_firmware;
	device_class->report_metadata_pre = fu_uefi_nvram_device_report_metadata_pre;
}
