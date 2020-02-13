/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2015-2017 Peter Jones <pjones@redhat.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>
#include <efivar.h>
#include <efivar/efiboot.h>

#include "fu-device-metadata.h"

#include "fu-common.h"

#include "fu-uefi-common.h"
#include "fu-uefi-device.h"
#include "fu-uefi-devpath.h"
#include "fu-uefi-bootmgr.h"
#include "fu-uefi-pcrs.h"
#include "fu-uefi-vars.h"
#include "fu-uefi-udisks.h"

struct _FuUefiDevice {
	FuDevice		 parent_instance;
	gchar			*fw_class;
	FuUefiDeviceKind	 kind;
	guint32			 capsule_flags;
	guint32			 fw_version;
	guint32			 fw_version_lowest;
	FuUefiDeviceStatus	 last_attempt_status;
	guint32			 last_attempt_version;
	guint64			 fmp_hardware_instance;
	gboolean		 missing_header;
	gboolean		 automounted_esp;
};

G_DEFINE_TYPE (FuUefiDevice, fu_uefi_device, FU_TYPE_DEVICE)

const gchar *
fu_uefi_device_kind_to_string (FuUefiDeviceKind kind)
{
	if (kind == FU_UEFI_DEVICE_KIND_UNKNOWN)
		return "unknown";
	if (kind == FU_UEFI_DEVICE_KIND_SYSTEM_FIRMWARE)
		return "system-firmware";
	if (kind == FU_UEFI_DEVICE_KIND_DEVICE_FIRMWARE)
		return "device-firmware";
	if (kind == FU_UEFI_DEVICE_KIND_UEFI_DRIVER)
		return "uefi-driver";
	if (kind == FU_UEFI_DEVICE_KIND_FMP)
		return "fmp";
	if (kind == FU_UEFI_DEVICE_KIND_DELL_TPM_FIRMWARE)
		return "dell-tpm-firmware";
	return NULL;
}

static FuUefiDeviceKind
fu_uefi_device_kind_from_string (const gchar *kind)
{
	if (g_strcmp0 (kind, "system-firmware") == 0)
		return FU_UEFI_DEVICE_KIND_SYSTEM_FIRMWARE;
	if (g_strcmp0 (kind, "device-firmware") == 0)
		return FU_UEFI_DEVICE_KIND_DEVICE_FIRMWARE;
	if (g_strcmp0 (kind, "uefi-driver") == 0)
		return FU_UEFI_DEVICE_KIND_UEFI_DRIVER;
	if (g_strcmp0 (kind, "fmp") == 0)
		return FU_UEFI_DEVICE_KIND_FMP;
	if (g_strcmp0 (kind, "dell-tpm-firmware") == 0)
		return FU_UEFI_DEVICE_KIND_DELL_TPM_FIRMWARE;
	return FU_UEFI_DEVICE_KIND_UNKNOWN;
}

const gchar *
fu_uefi_device_status_to_string (FuUefiDeviceStatus status)
{
	if (status == FU_UEFI_DEVICE_STATUS_SUCCESS)
		return "success";
	if (status == FU_UEFI_DEVICE_STATUS_ERROR_UNSUCCESSFUL)
		return "unsuccessful";
	if (status == FU_UEFI_DEVICE_STATUS_ERROR_INSUFFICIENT_RESOURCES)
		return "insufficient resources";
	if (status == FU_UEFI_DEVICE_STATUS_ERROR_INCORRECT_VERSION)
		return "incorrect version";
	if (status == FU_UEFI_DEVICE_STATUS_ERROR_INVALID_FORMAT)
		return "invalid firmware format";
	if (status == FU_UEFI_DEVICE_STATUS_ERROR_AUTH_ERROR)
		return "authentication signing error";
	if (status == FU_UEFI_DEVICE_STATUS_ERROR_PWR_EVT_AC)
		return "AC power required";
	if (status == FU_UEFI_DEVICE_STATUS_ERROR_PWR_EVT_BATT)
		return "battery level is too low";
	return NULL;
}

static void
fu_uefi_device_to_string (FuDevice *device, guint idt, GString *str)
{
	FuUefiDevice *self = FU_UEFI_DEVICE (device);
	fu_common_string_append_kv (str, idt, "Kind", fu_uefi_device_kind_to_string (self->kind));
	fu_common_string_append_kv (str, idt, "FwClass", self->fw_class);
	fu_common_string_append_kx (str, idt, "CapsuleFlags", self->capsule_flags);
	fu_common_string_append_kx (str, idt, "FwVersion", self->fw_version);
	fu_common_string_append_kx (str, idt, "FwVersionLowest", self->fw_version_lowest);
	fu_common_string_append_kv (str, idt, "LastAttemptStatus",
				    fu_uefi_device_status_to_string (self->last_attempt_status));
	fu_common_string_append_kx (str, idt, "LastAttemptVersion", self->last_attempt_version);
	fu_common_string_append_kv (str, idt, "EspPath",
				    fu_device_get_metadata (device, "EspPath"));
	fu_common_string_append_ku (str, idt, "RequireESPFreeSpace",
				    fu_device_get_metadata_integer (device, "RequireESPFreeSpace"));
	fu_common_string_append_kb (str, idt, "RequireShimForSecureBoot",
				    fu_device_get_metadata_boolean (device, "RequireShimForSecureBoot"));
}

FuUefiDeviceKind
fu_uefi_device_get_kind (FuUefiDevice *self)
{
	g_return_val_if_fail (FU_IS_UEFI_DEVICE (self), 0);
	return self->kind;
}

guint32
fu_uefi_device_get_version (FuUefiDevice *self)
{
	g_return_val_if_fail (FU_IS_UEFI_DEVICE (self), 0x0);
	return self->fw_version;
}

guint32
fu_uefi_device_get_version_lowest (FuUefiDevice *self)
{
	g_return_val_if_fail (FU_IS_UEFI_DEVICE (self), 0x0);
	return self->fw_version_lowest;
}

guint32
fu_uefi_device_get_version_error (FuUefiDevice *self)
{
	g_return_val_if_fail (FU_IS_UEFI_DEVICE (self), 0x0);
	return self->last_attempt_version;
}

guint64
fu_uefi_device_get_hardware_instance (FuUefiDevice *self)
{
	g_return_val_if_fail (FU_IS_UEFI_DEVICE (self), 0x0);
	return self->fmp_hardware_instance;
}

FuUefiDeviceStatus
fu_uefi_device_get_status (FuUefiDevice *self)
{
	g_return_val_if_fail (FU_IS_UEFI_DEVICE (self), 0);
	return self->last_attempt_status;
}

guint32
fu_uefi_device_get_capsule_flags (FuUefiDevice *self)
{
	g_return_val_if_fail (FU_IS_UEFI_DEVICE (self), 0x0);
	return self->capsule_flags;
}

const gchar *
fu_uefi_device_get_guid (FuUefiDevice *self)
{
	g_return_val_if_fail (FU_IS_UEFI_DEVICE (self), NULL);
	return self->fw_class;
}

static gchar *
fu_uefi_device_build_varname (FuUefiDevice *self)
{
	return g_strdup_printf ("fwupd-%s-%"G_GUINT64_FORMAT,
				self->fw_class,
				self->fmp_hardware_instance);
}

FuUefiUpdateInfo *
fu_uefi_device_load_update_info (FuUefiDevice *self, GError **error)
{
	gsize datasz = 0;
	g_autofree gchar *varname = fu_uefi_device_build_varname (self);
	g_autofree guint8 *data = NULL;
	g_autoptr(FuUefiUpdateInfo) info = fu_uefi_update_info_new ();

	g_return_val_if_fail (FU_IS_UEFI_DEVICE (self), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* get the existing status */
	if (!fu_uefi_vars_get_data (FU_UEFI_VARS_GUID_FWUPDATE, varname,
				    &data, &datasz, NULL, error))
		return NULL;
	if (!fu_uefi_update_info_parse (info, data, datasz, error))
		return NULL;
	return g_steal_pointer (&info);
}

gboolean
fu_uefi_device_clear_status (FuUefiDevice *self, GError **error)
{
	efi_update_info_t info;
	gsize datasz = 0;
	g_autofree gchar *varname = fu_uefi_device_build_varname (self);
	g_autofree guint8 *data = NULL;

	g_return_val_if_fail (FU_IS_UEFI_DEVICE (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* get the existing status */
	if (!fu_uefi_vars_get_data (FU_UEFI_VARS_GUID_FWUPDATE, varname,
				    &data, &datasz, NULL, error))
		return FALSE;
	if (datasz < sizeof(efi_update_info_t)) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "EFI variable is corrupt");
		return FALSE;
	}

	/* just copy the efi_update_info_t, ignore devpath then save it back */
	memcpy (&info, data, sizeof(info));
	info.status = FU_UEFI_DEVICE_STATUS_SUCCESS;
	memcpy (data, &info, sizeof(info));
	return fu_uefi_vars_set_data (FU_UEFI_VARS_GUID_FWUPDATE, varname,
				      data, datasz,
				      FU_UEFI_VARS_ATTR_NON_VOLATILE |
				      FU_UEFI_VARS_ATTR_BOOTSERVICE_ACCESS |
				      FU_UEFI_VARS_ATTR_RUNTIME_ACCESS,
				      error);
}

static guint8 *
fu_uefi_device_build_dp_buf (const gchar *path, gsize *bufsz, GError **error)
{
	gssize req;
	gssize sz;
	g_autofree guint8 *dp_buf = NULL;
	g_autoptr(GPtrArray) dps = NULL;

	/* get the size of the path first */
	req = efi_generate_file_device_path (NULL, 0, path,
					     EFIBOOT_OPTIONS_IGNORE_FS_ERROR |
					     EFIBOOT_ABBREV_HD);
	if (req < 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "failed to efi_generate_file_device_path(%s)",
			     path);
		return NULL;
	}

	/* if we just have an end device path, it's not going to work */
	if (req <= 4) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "failed to get valid device_path for (%s)",
			     path);
		return NULL;
	}

	/* actually get the path this time */
	dp_buf = g_malloc0 (req);
	sz = efi_generate_file_device_path (dp_buf, req, path,
					    EFIBOOT_OPTIONS_IGNORE_FS_ERROR |
					    EFIBOOT_ABBREV_HD);
	if (sz < 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "failed to efi_generate_file_device_path(%s)",
			     path);
		return NULL;
	}

	/* parse what we got back from efivar */
	dps = fu_uefi_devpath_parse (dp_buf, (gsize) sz,
				     FU_UEFI_DEVPATH_PARSE_FLAG_NONE, error);
	if (dps == NULL) {
		fu_common_dump_raw (G_LOG_DOMAIN, "dp_buf", dp_buf, (gsize) sz);
		return NULL;
	}

	/* success */
	if (bufsz != NULL)
		*bufsz = sz;
	return g_steal_pointer (&dp_buf);
}

static GBytes *
fu_uefi_device_fixup_firmware (FuDevice *device, GBytes *fw, GError **error)
{
	FuUefiDevice *self = FU_UEFI_DEVICE (device);
	gsize fw_length;
	efi_guid_t esrt_guid;
	efi_guid_t payload_guid;
	const gchar *data = g_bytes_get_data (fw, &fw_length);
	self->missing_header = FALSE;

	/* convert to EFI GUIDs */
	if (efi_str_to_guid (fu_uefi_device_get_guid (self), &esrt_guid) < 0) {
		g_set_error_literal (error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL,
				     "Invalid ESRT GUID");
		return NULL;
	}
	if (fw_length < sizeof(efi_guid_t)) {
		g_set_error_literal (error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE,
				     "Invalid payload");
		return NULL;
	}
	memcpy (&payload_guid, data, sizeof(efi_guid_t));

	/* ESRT header matches payload */
	if (efi_guid_cmp (&esrt_guid, &payload_guid) == 0) {
		g_debug ("ESRT matches payload GUID");
		return g_bytes_new_from_bytes (fw, 0, fw_length);
	/* FMP payload */
	} else if (fu_uefi_device_get_kind (self) == FU_UEFI_DEVICE_KIND_FMP) {
		g_debug ("performing FMP update");
		return g_bytes_new_from_bytes (fw, 0, fw_length);
	/* Missing, add a header */
	} else {
		guint header_size = getpagesize();
		guint8 *new_data = g_malloc (fw_length + header_size);
		guint8 *capsule = new_data + header_size;
		efi_capsule_header_t *header = (efi_capsule_header_t *) new_data;

		g_warning ("missing or invalid embedded capsule header");
		self->missing_header = TRUE;
		header->flags = self->capsule_flags;
		header->header_size = header_size;
		header->capsule_image_size = fw_length + header_size;
		memcpy (&header->guid, &esrt_guid, sizeof (efi_guid_t));
		memcpy (capsule, data, fw_length);

		return g_bytes_new_take (new_data, fw_length + header_size);
	}
}

gboolean
fu_uefi_missing_capsule_header (FuDevice *device)
{
	FuUefiDevice *self = FU_UEFI_DEVICE (device);
	return self->missing_header;
}

gboolean
fu_uefi_device_write_update_info (FuUefiDevice *self,
				  const gchar *filename,
				  const gchar *varname,
				  const efi_guid_t *guid,
				  GError **error)
{
	gsize datasz = 0;
	gsize dp_bufsz = 0;
	g_autofree guint8 *data = NULL;
	g_autofree guint8 *dp_buf = NULL;
	efi_update_info_t info = {
		.update_info_version	= 0x7,
		.guid			= { 0x0 },
		.capsule_flags		= self->capsule_flags,
		.hw_inst		= self->fmp_hardware_instance,
		.time_attempted		= { 0x0 },
		.status			= FU_UEFI_UPDATE_INFO_STATUS_ATTEMPT_UPDATE,
	};

	/* set the body as the device path */
	if (g_getenv ("FWUPD_UEFI_TEST") != NULL) {
		g_debug ("not building device path, in tests....");
		return TRUE;
	}

	/* convert to EFI device path */
	dp_buf = fu_uefi_device_build_dp_buf (filename, &dp_bufsz, error);
	if (dp_buf == NULL) {
		fu_uefi_print_efivar_errors ();
		return FALSE;
	}

	/* save this header and body to the hardware */
	memcpy (&info.guid, guid, sizeof(efi_guid_t));
	datasz = sizeof(info) + dp_bufsz;
	data = g_malloc0 (datasz);
	memcpy (data, &info, sizeof(info));
	memcpy (data + sizeof(info), dp_buf, dp_bufsz);
	if (!fu_uefi_vars_set_data (FU_UEFI_VARS_GUID_FWUPDATE, varname,
				    data, datasz,
				    FU_UEFI_VARS_ATTR_NON_VOLATILE |
				    FU_UEFI_VARS_ATTR_BOOTSERVICE_ACCESS |
				    FU_UEFI_VARS_ATTR_RUNTIME_ACCESS,
				    error)) {
		fu_uefi_print_efivar_errors ();
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_uefi_device_is_esp_mounted (FuDevice *device, GError **error)
{
	const gchar *esp_path = fu_device_get_metadata (device, "EspPath");
	g_autofree gchar *contents = NULL;
	g_auto(GStrv) lines = NULL;
	gsize length;

	if (esp_path == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "EFI System partition is not defined");
		return FALSE;
	}

	if (!g_file_get_contents ("/proc/mounts", &contents, &length, error))
		return FALSE;
	lines = g_strsplit (contents, "\n", 0);

	for (guint i = 0; lines[i] != NULL; i++) {
		if (lines[i] != NULL && g_strrstr (lines[i], esp_path))
			return TRUE;
	}
	g_set_error (error,
		     FWUPD_ERROR,
		     FWUPD_ERROR_NOT_SUPPORTED,
		     "EFI System partition %s is not mounted",
		     esp_path);
	return FALSE;
}

static gboolean
fu_uefi_device_check_esp_free (FuDevice *device, GError **error)
{
	const gchar *esp_path = fu_device_get_metadata (device, "EspPath");
	guint64 sz_reqd = fu_device_get_metadata_integer (device, "RequireESPFreeSpace");
	if (sz_reqd == G_MAXUINT) {
		g_debug ("maximum size is not configured");
		return TRUE;
	}
	return 	fu_uefi_check_esp_free_space (esp_path, sz_reqd, error);
}

static gboolean
fu_uefi_device_cleanup_esp (FuDevice *device, GError **error)
{
	const gchar *esp_path = fu_device_get_metadata (device, "EspPath");
	g_autofree gchar *pattern = NULL;
	g_autoptr(GPtrArray) files = NULL;

	/* in case we call capsule install twice before reboot */
	if (fu_uefi_vars_exists (FU_UEFI_VARS_GUID_EFI_GLOBAL, "BootNext"))
		return TRUE;

	/* delete any files matching the glob in the ESP */
	files = fu_common_get_files_recursive (esp_path, error);
	if (files == NULL)
		return FALSE;
	pattern = g_build_filename (esp_path, "EFI/*/fw/fwupd*.cap", NULL);
	for (guint i = 0; i < files->len; i++) {
		const gchar *fn = g_ptr_array_index (files, i);
		if (fu_common_fnmatch (pattern, fn)) {
			g_autoptr(GFile) file = g_file_new_for_path (fn);
			g_debug ("deleting %s", fn);
			if (!g_file_delete (file, NULL, error))
				return FALSE;
		}
	}

	/* delete any old variables */
	if (!fu_uefi_vars_delete_with_glob (FU_UEFI_VARS_GUID_FWUPDATE, "fwupd*-*", error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_uefi_device_prepare (FuDevice *device,
			FwupdInstallFlags flags,
			GError **error)
{
	/* not set in conf, figure it out */
	if (fu_device_get_metadata (device, "EspPath") == NULL) {
		g_autofree gchar *guessed = NULL;
		g_autofree gchar *detected_esp = NULL;
		guessed = fu_uefi_guess_esp_path (error);
		if (guessed == NULL)
			return FALSE;

		/* udisks objpath */
		if (fu_uefi_udisks_objpath (guessed)) {
			FuUefiDevice *self = FU_UEFI_DEVICE (device);
			detected_esp = fu_uefi_udisks_objpath_is_mounted (guessed);
			if (detected_esp != NULL) {
				g_debug ("ESP already mounted @ %s", detected_esp);
			/* not mounted */
			} else {
				g_debug ("Mounting ESP @ %s", guessed);
				detected_esp = fu_uefi_udisks_objpath_mount (guessed, error);
				if (detected_esp == NULL)
					return FALSE;
				self->automounted_esp = TRUE;
			}
		/* already mounted */
		} else {
			detected_esp = g_steal_pointer (&guessed);
		}
		fu_device_set_metadata (device, "EspPath", detected_esp);
	}

	/* sanity checks */
	if (!fu_uefi_device_is_esp_mounted (device, error))
		return FALSE;
	if (!fu_uefi_device_check_esp_free (device, error))
		return FALSE;
	if (!fu_uefi_device_cleanup_esp (device, error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_uefi_device_cleanup (FuDevice *device,
			FwupdInstallFlags flags,
			GError **error)
{
	FuUefiDevice *self = FU_UEFI_DEVICE (device);
	if (self->automounted_esp) {
		g_autofree gchar *guessed = NULL;
		guessed = fu_uefi_guess_esp_path (error);
		if (guessed == NULL)
			return FALSE;
		g_debug ("Unmounting ESP @ %s", guessed);
		if (!fu_uefi_udisks_objpath_umount (guessed, error))
			return FALSE;
		self->automounted_esp = FALSE;
		/* we will detect again if necessary */
		fu_device_remove_metadata (device, "EspPath");
	}

	return TRUE;
}

static gboolean
fu_uefi_device_write_firmware (FuDevice *device,
			       FuFirmware *firmware,
			       FwupdInstallFlags install_flags,
			       GError **error)
{
	FuUefiDevice *self = FU_UEFI_DEVICE (device);
	FuUefiBootmgrFlags flags = FU_UEFI_BOOTMGR_FLAG_NONE;
	const gchar *bootmgr_desc = "Linux Firmware Updater";
	const gchar *esp_path = fu_device_get_metadata (device, "EspPath");
	efi_guid_t guid;
	g_autoptr(GBytes) fixed_fw = NULL;
	g_autoptr(GBytes) fw = NULL;
	g_autofree gchar *basename = NULL;
	g_autofree gchar *directory = NULL;
	g_autofree gchar *fn = NULL;
	g_autofree gchar *varname = fu_uefi_device_build_varname (self);

	/* ensure we have the existing state */
	if (self->fw_class == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "cannot update device info with no GUID");
		return FALSE;
	}

	/* get default image */
	fw = fu_firmware_get_image_default_bytes (firmware, error);
	if (fw == NULL)
		return FALSE;

	/* save the blob to the ESP */
	directory = fu_uefi_get_esp_path_for_os (esp_path);
	basename = g_strdup_printf ("fwupd-%s.cap", self->fw_class);
	fn = g_build_filename (directory, "fw", basename, NULL);
	if (!fu_common_mkdir_parent (fn, error))
		return FALSE;
	fixed_fw = fu_uefi_device_fixup_firmware (device, fw, error);
	if (fixed_fw == NULL)
		return FALSE;
	if (!fu_common_set_contents_bytes (fn, fixed_fw, error))
		return FALSE;

	/* set the blob header shared with fwupd.efi */
	if (efi_str_to_guid (self->fw_class, &guid) < 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "failed to get convert GUID");
		return FALSE;
	}
	if (!fu_uefi_device_write_update_info (self, fn, varname, &guid, error))
		return FALSE;

	/* update the firmware before the bootloader runs */
	if (fu_device_get_metadata_boolean (device, "RequireShimForSecureBoot"))
		flags |= FU_UEFI_BOOTMGR_FLAG_USE_SHIM_FOR_SB;
	if (fu_device_has_custom_flag (device, "use-shim-unique"))
		flags |= FU_UEFI_BOOTMGR_FLAG_USE_SHIM_UNIQUE;

	/* some legacy devices use the old name to deduplicate boot entries */
	if (fu_device_has_custom_flag (device, "use-legacy-bootmgr-desc"))
		bootmgr_desc = "Linux-Firmware-Updater";
	if (!fu_uefi_bootmgr_bootnext (esp_path, bootmgr_desc, flags, error))
		return FALSE;

	/* success! */
	return TRUE;
}

static gboolean
fu_uefi_device_add_system_checksum (FuDevice *device, GError **error)
{
	g_autoptr(FuUefiPcrs) pcrs = fu_uefi_pcrs_new ();
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GPtrArray) pcr0s = NULL;

	/* get all the PCRs */
	if (!fu_uefi_pcrs_setup (pcrs, &error_local)) {
		if (g_error_matches (error_local,
				     G_IO_ERROR,
				     G_IO_ERROR_NOT_SUPPORTED) ||
		    g_error_matches (error_local,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_FOUND)) {
			g_debug ("%s", error_local->message);
			return TRUE;
		}
		g_propagate_error (error, g_steal_pointer (&error_local));
		return FALSE;
	}

	/* get all the PCR0s */
	pcr0s = fu_uefi_pcrs_get_checksums (pcrs, 0);
	if (pcr0s->len == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "no PCR0s detected");
		return FALSE;
	}
	for (guint i = 0; i < pcr0s->len; i++) {
		const gchar *checksum = g_ptr_array_index (pcr0s, i);
		fu_device_add_checksum (device, checksum);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_uefi_device_probe (FuDevice *device, GError **error)
{
	FuUefiDevice *self = FU_UEFI_DEVICE (device);
	FwupdVersionFormat version_format;
	g_autofree gchar *devid = NULL;
	g_autofree gchar *guid_strup = NULL;
	g_autofree gchar *version_lowest = NULL;
	g_autofree gchar *version = NULL;

	/* broken sysfs? */
	if (self->fw_class == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "failed to read fw_class");
		return FALSE;
	}

	/* add GUID first, as quirks may set the version format */
	fu_device_add_guid (device, self->fw_class);

	/* set versions */
	version_format = fu_device_get_version_format (device);
	version = fu_common_version_from_uint32 (self->fw_version, version_format);
	fu_device_set_version (device, version, version_format);
	fu_device_set_version_raw (device, self->fw_version);
	if (self->fw_version_lowest != 0) {
		version_lowest = fu_common_version_from_uint32 (self->fw_version_lowest,
							        version_format);
		fu_device_set_version_lowest (device, version_lowest);
	}

	/* set flags */
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_NEEDS_REBOOT);
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_REQUIRE_AC);

	/* add icons */
	if (self->kind == FU_UEFI_DEVICE_KIND_DEVICE_FIRMWARE) {
		/* nothing better in the icon naming spec */
		fu_device_add_icon (device, "audio-card");
	} else {
		/* this is probably system firmware */
		fu_device_add_icon (device, "computer");
		fu_device_add_instance_id (device, "main-system-firmware");
	}

	/* set the PCR0 as the device checksum */
	if (self->kind == FU_UEFI_DEVICE_KIND_SYSTEM_FIRMWARE) {
		g_autoptr(GError) error_local = NULL;
		fu_device_add_flag (device, FWUPD_DEVICE_FLAG_CAN_VERIFY);
		if (!fu_uefi_device_add_system_checksum (device, &error_local))
			g_warning ("Failed to get PCR0s: %s", error_local->message);
	}

	/* Windows seems to be case insensitive, but for convenience we'll
	 * match the upper case values typically specified in the .inf file */
	guid_strup = g_ascii_strup (self->fw_class, -1);
	devid = g_strdup_printf ("UEFI\\RES_{%s}", guid_strup);
	fu_device_add_instance_id (device, devid);
	return TRUE;
}

static void
fu_uefi_device_init (FuUefiDevice *self)
{
	fu_device_set_protocol (FU_DEVICE (self), "org.uefi.capsule");
}

static void
fu_uefi_device_finalize (GObject *object)
{
	FuUefiDevice *self = FU_UEFI_DEVICE (object);

	g_free (self->fw_class);

	G_OBJECT_CLASS (fu_uefi_device_parent_class)->finalize (object);
}

static void
fu_uefi_device_class_init (FuUefiDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	object_class->finalize = fu_uefi_device_finalize;
	klass_device->to_string = fu_uefi_device_to_string;
	klass_device->probe = fu_uefi_device_probe;
	klass_device->prepare = fu_uefi_device_prepare;
	klass_device->write_firmware = fu_uefi_device_write_firmware;
	klass_device->cleanup = fu_uefi_device_cleanup;
}

FuUefiDevice *
fu_uefi_device_new_from_entry (const gchar *entry_path, GError **error)
{
	g_autoptr(FuUefiDevice) self = NULL;
	g_autofree gchar *fw_class_fn = NULL;
	g_autofree gchar *id = NULL;

	g_return_val_if_fail (entry_path != NULL, NULL);

	/* create object */
	self = g_object_new (FU_TYPE_UEFI_DEVICE, NULL);

	/* read values from sysfs */
	fw_class_fn = g_build_filename (entry_path, "fw_class", NULL);
	if (g_file_get_contents (fw_class_fn, &self->fw_class, NULL, NULL))
		g_strdelimit (self->fw_class, "\n", '\0');
	self->capsule_flags = fu_uefi_read_file_as_uint64 (entry_path, "capsule_flags");
	self->kind = fu_uefi_read_file_as_uint64 (entry_path, "fw_type");
	self->fw_version = fu_uefi_read_file_as_uint64 (entry_path, "fw_version");
	self->last_attempt_status = fu_uefi_read_file_as_uint64 (entry_path, "last_attempt_status");
	self->last_attempt_version = fu_uefi_read_file_as_uint64 (entry_path, "last_attempt_version");
	self->fw_version_lowest = fu_uefi_read_file_as_uint64 (entry_path, "lowest_supported_fw_version");

	/* the hardware instance is not in the ESRT table and we should really
	 * write the EFI stub to query with FMP -- but we still have not ever
	 * seen a PCIe device with FMP support... */
	self->fmp_hardware_instance = 0x0;

	/* set ID */
	id = g_strdup_printf ("UEFI-%s-dev%" G_GUINT64_FORMAT,
			      self->fw_class, self->fmp_hardware_instance);
	fu_device_set_id (FU_DEVICE (self), id);

	/* this is invalid */
	if (!fwupd_guid_is_valid (self->fw_class)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "ESRT GUID '%s' was not valid", self->fw_class);
		return NULL;
	}

	return g_steal_pointer (&self);
}

FuUefiDevice *
fu_uefi_device_new_from_dev (FuDevice *dev)
{
	const gchar *tmp;
	FuUefiDevice *self;

	g_return_val_if_fail (fu_device_get_guid_default (dev) != NULL, NULL);

	/* create virtual object not backed by an ESRT entry */
	self = g_object_new (FU_TYPE_UEFI_DEVICE, NULL);
	fu_device_incorporate (FU_DEVICE (self), dev);
	self->fw_class = g_strdup (fu_device_get_guid_default (dev));
	tmp = fu_device_get_metadata (dev, FU_DEVICE_METADATA_UEFI_DEVICE_KIND);
	self->kind = fu_uefi_device_kind_from_string (tmp);
	self->capsule_flags = fu_device_get_metadata_integer (dev, FU_DEVICE_METADATA_UEFI_CAPSULE_FLAGS);
	self->fw_version = fu_device_get_metadata_integer (dev, FU_DEVICE_METADATA_UEFI_FW_VERSION);
	g_assert (self->fw_class != NULL);
	return self;
}

FuUefiDevice *
fu_uefi_device_new_from_guid (const gchar *guid)
{
	FuUefiDevice *self;
	self = g_object_new (FU_TYPE_UEFI_DEVICE, NULL);
	self->fw_class = g_strdup (guid);
	return self;
}
