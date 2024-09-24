/*
 * Copyright 2021 Mario Limonciello <mario.limonciello@amd.com>
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-uefi-common.h"
#include "fu-uefi-grub-device.h"

struct _FuUefiGrubDevice {
	FuUefiDevice parent_instance;
};

G_DEFINE_TYPE(FuUefiGrubDevice, fu_uefi_grub_device, FU_TYPE_UEFI_DEVICE)

static gboolean
fu_uefi_grub_device_mkconfig(FuDevice *device,
			     const gchar *esp_path,
			     const gchar *target_app,
			     GError **error)
{
	const gchar *argv_mkconfig[] = {"", "-o", "/boot/grub/grub.cfg", NULL};
	const gchar *argv_reboot[] = {"", "fwupd", NULL};
	g_autofree gchar *grub_mkconfig = NULL;
	g_autofree gchar *grub_reboot = NULL;
	g_autofree gchar *grub_target = NULL;
	g_autofree gchar *localstatedir = fu_path_from_kind(FU_PATH_KIND_LOCALSTATEDIR_PKG);
	g_autofree gchar *output = NULL;
	g_autoptr(GString) str = g_string_new(NULL);

	/* find grub.conf */
	if (!g_file_test(argv_mkconfig[2], G_FILE_TEST_EXISTS))
		argv_mkconfig[2] = "/boot/grub2/grub.cfg";
	if (!g_file_test(argv_mkconfig[2], G_FILE_TEST_EXISTS)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "could not find grub.conf");
		return FALSE;
	}

	/* find grub-mkconfig */
	grub_mkconfig = fu_path_find_program("grub-mkconfig", NULL);
	if (grub_mkconfig == NULL)
		grub_mkconfig = fu_path_find_program("grub2-mkconfig", NULL);
	if (grub_mkconfig == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "could not find grub-mkconfig");
		return FALSE;
	}

	/* find grub-reboot */
	grub_reboot = fu_path_find_program("grub-reboot", NULL);
	if (grub_reboot == NULL)
		grub_reboot = fu_path_find_program("grub2-reboot", NULL);
	if (grub_reboot == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "could not find grub-reboot");
		return FALSE;
	}

	/* replace ESP info in conf with what we detected */
	g_string_append_printf(str, "EFI_PATH=%s\n", target_app);
	g_string_replace(str, esp_path, "", 0);
	g_string_append_printf(str, "ESP=%s\n", esp_path);
	grub_target = g_build_filename(localstatedir, "uefi_capsule.conf", NULL);
	if (!g_file_set_contents(grub_target, str->str, -1, error))
		return FALSE;

	/* refresh GRUB configuration */
	argv_mkconfig[0] = grub_mkconfig;
	if (!g_spawn_sync(NULL,
			  (gchar **)argv_mkconfig,
			  NULL,
			  G_SPAWN_DEFAULT,
			  NULL,
			  NULL,
			  &output,
			  NULL,
			  NULL,
			  error))
		return FALSE;
	g_debug("%s", output);

	/* make fwupd default */
	argv_reboot[0] = grub_reboot;
	return g_spawn_sync(NULL,
			    (gchar **)argv_reboot,
			    NULL,
			    G_SPAWN_DEFAULT,
			    NULL,
			    NULL,
			    NULL,
			    NULL,
			    NULL,
			    error);
}

static gboolean
fu_uefi_grub_device_write_firmware(FuDevice *device,
				   FuFirmware *firmware,
				   FuProgress *progress,
				   FwupdInstallFlags flags,
				   GError **error)
{
	FuContext *ctx = fu_device_get_context(device);
	FuEfivars *efivars = fu_context_get_efivars(ctx);
	FuUefiDevice *self = FU_UEFI_DEVICE(device);
	FuVolume *esp = fu_uefi_device_get_esp(self);
	const gchar *fw_class = fu_uefi_device_get_guid(self);
	g_autofree gchar *basename = NULL;
	g_autofree gchar *capsule_path = NULL;
	g_autofree gchar *directory = NULL;
	g_autofree gchar *esp_path = fu_volume_get_mount_point(esp);
	g_autofree gchar *fn = NULL;
	g_autofree gchar *source_app = NULL;
	g_autofree gchar *target_app = NULL;
	g_autofree gchar *varname = fu_uefi_device_build_varname(self);
	g_autoptr(GBytes) fixed_fw = NULL;
	g_autoptr(GBytes) fw = NULL;

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

	/* skip for self tests */
	if (g_getenv("FWUPD_UEFI_TEST") != NULL)
		return TRUE;

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

	/* if secure boot was turned on this might need to be installed separately */
	source_app = fu_uefi_get_built_app_path(efivars, "fwupd", error);
	if (source_app == NULL)
		return FALSE;

	/* test if correct asset in place */
	target_app = fu_uefi_get_esp_app_path(esp_path, "fwupd", error);
	if (target_app == NULL)
		return FALSE;
	if (!fu_uefi_esp_target_verify(source_app, esp, target_app)) {
		if (!fu_uefi_esp_target_copy(source_app, esp, target_app, error))
			return FALSE;
	}

	/* we are using GRUB instead of NVRAM variables */
	return fu_uefi_grub_device_mkconfig(device, esp_path, target_app, error);
}

static void
fu_uefi_grub_device_report_metadata_pre(FuDevice *device, GHashTable *metadata)
{
	/* FuUefiDevice */
	FU_DEVICE_CLASS(fu_uefi_grub_device_parent_class)->report_metadata_pre(device, metadata);
	g_hash_table_insert(metadata, g_strdup("CapsuleApplyMethod"), g_strdup("grub"));
}

static void
fu_uefi_grub_device_init(FuUefiGrubDevice *self)
{
	fu_device_set_summary(FU_DEVICE(self),
			      "UEFI System Resource Table device (updated via grub)");
}

static void
fu_uefi_grub_device_class_init(FuUefiGrubDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->write_firmware = fu_uefi_grub_device_write_firmware;
	device_class->report_metadata_pre = fu_uefi_grub_device_report_metadata_pre;
}
