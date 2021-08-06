/*
 * Copyright (C) 2021 Mario Limonciello <mario.limonciello@amd.com>
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

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
	g_autofree gchar *localstatedir = fu_common_get_path(FU_PATH_KIND_LOCALSTATEDIR_PKG);
	g_autofree gchar *output = NULL;
	g_autoptr(GString) str = g_string_new(NULL);

	/* find grub.conf */
	if (!g_file_test(argv_mkconfig[2], G_FILE_TEST_EXISTS))
		argv_mkconfig[2] = "/boot/grub2/grub.cfg";
	if (!g_file_test(argv_mkconfig[2], G_FILE_TEST_EXISTS)) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_FAILED,
				    "could not find grub.conf");
		return FALSE;
	}

	/* find grub-mkconfig */
	grub_mkconfig = fu_common_find_program_in_path("grub-mkconfig", NULL);
	if (grub_mkconfig == NULL)
		grub_mkconfig = fu_common_find_program_in_path("grub2-mkconfig", NULL);
	if (grub_mkconfig == NULL) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_FAILED,
				    "could not find grub-mkconfig");
		return FALSE;
	}

	/* find grub-reboot */
	grub_reboot = fu_common_find_program_in_path("grub-reboot", NULL);
	if (grub_reboot == NULL)
		grub_reboot = fu_common_find_program_in_path("grub2-reboot", NULL);
	if (grub_reboot == NULL) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_FAILED,
				    "could not find grub-reboot");
		return FALSE;
	}

	/* replace ESP info in conf with what we detected */
	g_string_append_printf(str, "EFI_PATH=%s\n", target_app);
	fu_common_string_replace(str, esp_path, "");
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
	if (g_getenv("FWUPD_UPDATE_CAPSULE_VERBOSE") != NULL)
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
	FuUefiDevice *self = FU_UEFI_DEVICE(device);
	const gchar *fw_class = fu_uefi_device_get_guid(self);
	g_autofree gchar *basename = NULL;
	g_autofree gchar *directory = NULL;
	g_autofree gchar *esp_path = fu_uefi_device_get_esp_path(self);
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
	directory = fu_uefi_get_esp_path_for_os(device, esp_path);
	basename = g_strdup_printf("fwupd-%s.cap", fw_class);
	fn = g_build_filename(directory, "fw", basename, NULL);
	if (!fu_common_mkdir_parent(fn, error))
		return FALSE;
	fixed_fw = fu_uefi_device_fixup_firmware(self, fw, error);
	if (fixed_fw == NULL)
		return FALSE;
	if (!fu_common_set_contents_bytes(fn, fixed_fw, error))
		return FALSE;

	/* skip for self tests */
	if (g_getenv("FWUPD_UEFI_TEST") != NULL)
		return TRUE;

	/* delete the logs to save space; use fwupdate to debug the EFI binary */
	if (fu_efivar_exists(FU_EFIVAR_GUID_FWUPDATE, "FWUPDATE_VERBOSE")) {
		if (!fu_efivar_delete(FU_EFIVAR_GUID_FWUPDATE, "FWUPDATE_VERBOSE", error))
			return FALSE;
	}
	if (fu_efivar_exists(FU_EFIVAR_GUID_FWUPDATE, "FWUPDATE_DEBUG_LOG")) {
		if (!fu_efivar_delete(FU_EFIVAR_GUID_FWUPDATE, "FWUPDATE_DEBUG_LOG", error))
			return FALSE;
	}

	/* set the blob header shared with fwupd.efi */
	if (!fu_uefi_device_write_update_info(self, fn, varname, fw_class, error))
		return FALSE;

	/* if secure boot was turned on this might need to be installed separately */
	source_app = fu_uefi_get_built_app_path(error);
	if (source_app == NULL)
		return FALSE;

	/* test if correct asset in place */
	target_app = fu_uefi_get_esp_app_path(device, esp_path, "fwupd", error);
	if (target_app == NULL)
		return FALSE;
	if (!fu_uefi_cmp_asset(source_app, target_app)) {
		if (!fu_uefi_copy_asset(source_app, target_app, error))
			return FALSE;
	}

	/* we are using GRUB instead of NVRAM variables */
	return fu_uefi_grub_device_mkconfig(device, esp_path, target_app, error);
}

static void
fu_uefi_grub_device_init(FuUefiGrubDevice *self)
{
}

static void
fu_uefi_grub_device_class_init(FuUefiGrubDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->write_firmware = fu_uefi_grub_device_write_firmware;
}
