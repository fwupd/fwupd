/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2015 Peter Jones <pjones@redhat.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fu-uefi-common.h"
#include "fu-uefi-device.h"
#include "fu-uefi-struct.h"

typedef struct {
	FuVolume *esp;
	FuDeviceLocker *esp_locker;
	gchar *fw_class;
	FuUefiDeviceKind kind;
	guint32 capsule_flags;
	guint32 fw_version;
	guint32 fw_version_lowest;
	FuUefiDeviceStatus last_attempt_status;
	guint32 last_attempt_version;
	guint64 fmp_hardware_instance;
	gboolean missing_header;
	gboolean automounted_esp;
	gsize require_esp_free_space;
} FuUefiDevicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuUefiDevice, fu_uefi_device, FU_TYPE_DEVICE)

#define GET_PRIVATE(o) (fu_uefi_device_get_instance_private(o))

#define FU_EFI_FMP_CAPSULE_GUID "6dcbd5ed-e82d-4c44-bda1-7194199ad92a"

enum {
	PROP_0,
	PROP_FW_CLASS,
	PROP_KIND,
	PROP_CAPSULE_FLAGS,
	PROP_FW_VERSION,
	PROP_FW_VERSION_LOWEST,
	PROP_LAST_ATTEMPT_STATUS,
	PROP_LAST_ATTEMPT_VERSION,
	PROP_FMP_HARDWARE_INSTANCE,
	PROP_LAST
};

void
fu_uefi_device_set_esp(FuUefiDevice *self, FuVolume *esp)
{
	FuUefiDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_UEFI_DEVICE(self));
	g_return_if_fail(FU_IS_VOLUME(esp));
	g_set_object(&priv->esp, esp);
}

static void
fu_uefi_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuUefiDevice *self = FU_UEFI_DEVICE(device);
	FuUefiDevicePrivate *priv = GET_PRIVATE(self);

	fu_string_append(str, idt, "Kind", fu_uefi_device_kind_to_string(priv->kind));
	fu_string_append(str, idt, "FwClass", priv->fw_class);
	fu_string_append_kx(str, idt, "CapsuleFlags", priv->capsule_flags);
	fu_string_append_kx(str, idt, "FwVersion", priv->fw_version);
	fu_string_append_kx(str, idt, "FwVersionLowest", priv->fw_version_lowest);
	fu_string_append(str,
			 idt,
			 "LastAttemptStatus",
			 fu_uefi_device_status_to_string(priv->last_attempt_status));
	fu_string_append_kx(str, idt, "LastAttemptVersion", priv->last_attempt_version);
	if (priv->esp != NULL) {
		g_autofree gchar *kind = fu_volume_get_partition_kind(priv->esp);
		g_autofree gchar *mount_point = fu_volume_get_mount_point(priv->esp);
		fu_string_append(str, idt, "EspId", fu_volume_get_id(priv->esp));
		if (mount_point != NULL)
			fu_string_append(str, idt, "EspPath", mount_point);
		if (kind != NULL) {
			const gchar *guid = fu_volume_kind_convert_to_gpt(kind);
			fu_string_append(str, idt, "EspKind", kind);
			if (g_strcmp0(kind, guid) != 0)
				fu_string_append(str, idt, "EspGuid", guid);
		}
	}
	fu_string_append_ku(str, idt, "RequireESPFreeSpace", priv->require_esp_free_space);
}

static void
fu_uefi_device_report_metadata_pre(FuDevice *device, GHashTable *metadata)
{
	FuUefiDevice *self = FU_UEFI_DEVICE(device);
	FuUefiDevicePrivate *priv = GET_PRIVATE(self);
	g_autofree gchar *kind = fu_volume_get_partition_kind(priv->esp);
	g_autofree gchar *mount_point = fu_volume_get_mount_point(priv->esp);

	/* record if we had an invalid header during update */
	g_hash_table_insert(metadata,
			    g_strdup("MissingCapsuleHeader"),
			    g_strdup(priv->missing_header ? "True" : "False"));

	/* where and how the ESP was mounted during installation */
	g_hash_table_insert(metadata, g_strdup("EspPath"), g_steal_pointer(&mount_point));
	if (kind != NULL) {
		g_hash_table_insert(metadata, g_strdup("EspKind"), g_steal_pointer(&kind));
	}
}

static void
fu_uefi_device_report_metadata_post(FuDevice *device, GHashTable *metadata)
{
	FuUefiDevice *self = FU_UEFI_DEVICE(device);
	FuUefiDevicePrivate *priv = GET_PRIVATE(self);

	/* the actual last_attempt values */
	g_hash_table_insert(metadata,
			    g_strdup("LastAttemptStatus"),
			    g_strdup_printf("0x%x", priv->last_attempt_status));
	g_hash_table_insert(metadata,
			    g_strdup("LastAttemptVersion"),
			    g_strdup_printf("0x%x", priv->last_attempt_version));
}

FuUefiDeviceKind
fu_uefi_device_get_kind(FuUefiDevice *self)
{
	FuUefiDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_UEFI_DEVICE(self), 0);
	return priv->kind;
}

guint32
fu_uefi_device_get_version(FuUefiDevice *self)
{
	FuUefiDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_UEFI_DEVICE(self), 0x0);
	return priv->fw_version;
}

guint32
fu_uefi_device_get_version_lowest(FuUefiDevice *self)
{
	FuUefiDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_UEFI_DEVICE(self), 0x0);
	return priv->fw_version_lowest;
}

guint32
fu_uefi_device_get_version_error(FuUefiDevice *self)
{
	FuUefiDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_UEFI_DEVICE(self), 0x0);
	return priv->last_attempt_version;
}

guint64
fu_uefi_device_get_hardware_instance(FuUefiDevice *self)
{
	FuUefiDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_UEFI_DEVICE(self), 0x0);
	return priv->fmp_hardware_instance;
}

FuUefiDeviceStatus
fu_uefi_device_get_status(FuUefiDevice *self)
{
	FuUefiDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_UEFI_DEVICE(self), 0);
	return priv->last_attempt_status;
}

void
fu_uefi_device_set_status(FuUefiDevice *self, FuUefiDeviceStatus status)
{
	FuUefiDevicePrivate *priv = GET_PRIVATE(self);
	const gchar *tmp;
	g_autofree gchar *err_msg = NULL;
	g_autofree gchar *version_str = NULL;

	g_return_if_fail(FU_IS_UEFI_DEVICE(self));

	/* cache for later */
	priv->last_attempt_status = status;

	/* all good */
	if (status == FU_UEFI_DEVICE_STATUS_SUCCESS) {
		fu_device_set_update_state(FU_DEVICE(self), FWUPD_UPDATE_STATE_SUCCESS);
		return;
	}

	/* something went wrong */
	if (status == FU_UEFI_DEVICE_STATUS_ERROR_PWR_EVT_AC ||
	    status == FU_UEFI_DEVICE_STATUS_ERROR_PWR_EVT_BATT) {
		fu_device_set_update_state(FU_DEVICE(self), FWUPD_UPDATE_STATE_FAILED_TRANSIENT);
	} else {
		fu_device_set_update_state(FU_DEVICE(self), FWUPD_UPDATE_STATE_FAILED);
	}
	version_str = g_strdup_printf("%u", priv->last_attempt_version);
	tmp = fu_uefi_device_status_to_string(status);
	if (tmp == NULL) {
		err_msg = g_strdup_printf("failed to update to %s", version_str);
	} else {
		err_msg = g_strdup_printf("failed to update to %s: %s", version_str, tmp);
	}
	fu_device_set_update_error(FU_DEVICE(self), err_msg);
}

void
fu_uefi_device_set_require_esp_free_space(FuUefiDevice *self, gsize require_esp_free_space)
{
	FuUefiDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_UEFI_DEVICE(self));
	priv->require_esp_free_space = require_esp_free_space;
}

guint32
fu_uefi_device_get_capsule_flags(FuUefiDevice *self)
{
	FuUefiDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_UEFI_DEVICE(self), 0x0);
	return priv->capsule_flags;
}

const gchar *
fu_uefi_device_get_guid(FuUefiDevice *self)
{
	FuUefiDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_UEFI_DEVICE(self), NULL);
	return priv->fw_class;
}

gchar *
fu_uefi_device_build_varname(FuUefiDevice *self)
{
	FuUefiDevicePrivate *priv = GET_PRIVATE(self);
	return g_strdup_printf("fwupd-%s-%" G_GUINT64_FORMAT,
			       priv->fw_class,
			       priv->fmp_hardware_instance);
}

FuUefiUpdateInfo *
fu_uefi_device_load_update_info(FuUefiDevice *self, GError **error)
{
	g_autofree gchar *varname = fu_uefi_device_build_varname(self);
	g_autoptr(FuUefiUpdateInfo) info = fu_uefi_update_info_new();
	g_autoptr(GBytes) fw = NULL;

	g_return_val_if_fail(FU_IS_UEFI_DEVICE(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* get the existing status */
	fw = fu_efivar_get_data_bytes(FU_EFIVAR_GUID_FWUPDATE, varname, NULL, error);
	if (fw == NULL)
		return NULL;
	if (!fu_firmware_parse(FU_FIRMWARE(info), fw, FWUPD_INSTALL_FLAG_NONE, error))
		return NULL;
	return g_steal_pointer(&info);
}

gboolean
fu_uefi_device_clear_status(FuUefiDevice *self, GError **error)
{
	gsize datasz = 0;
	g_autofree gchar *varname = fu_uefi_device_build_varname(self);
	g_autofree guint8 *data = NULL;
	g_autoptr(GByteArray) st_inf = NULL;

	g_return_val_if_fail(FU_IS_UEFI_DEVICE(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* get the existing status */
	if (!fu_efivar_get_data(FU_EFIVAR_GUID_FWUPDATE, varname, &data, &datasz, NULL, error))
		return FALSE;
	st_inf = fu_struct_efi_update_info_parse(data, datasz, 0x0, error);
	if (st_inf == NULL) {
		g_prefix_error(error, "EFI variable is corrupt: ");
		return FALSE;
	}

	/* just copy the new EfiUpdateInfo and save it back */
	fu_struct_efi_update_info_set_status(st_inf, FU_UEFI_UPDATE_INFO_STATUS_UNKNOWN);
	memcpy(data, st_inf->data, st_inf->len);
	return fu_efivar_set_data(FU_EFIVAR_GUID_FWUPDATE,
				  varname,
				  data,
				  datasz,
				  FU_EFIVAR_ATTR_NON_VOLATILE | FU_EFIVAR_ATTR_BOOTSERVICE_ACCESS |
				      FU_EFIVAR_ATTR_RUNTIME_ACCESS,
				  error);
}

FuEfiDevicePathList *
fu_uefi_device_build_dp_buf(FuVolume *esp, const gchar *capsule_path, GError **error)
{
	g_autoptr(FuEfiDevicePathList) dp_buf = fu_efi_device_path_list_new();
	g_autoptr(FuEfiFilePathDevicePath) dp_file = fu_efi_file_path_device_path_new();
	g_autoptr(FuEfiHardDriveDevicePath) dp_hd = NULL;
	g_autofree gchar *name_with_root = NULL;

	dp_hd = fu_efi_hard_drive_device_path_new_from_volume(esp, error);
	if (dp_hd == NULL)
		return NULL;
	name_with_root = g_strdup_printf("/%s", capsule_path);
	if (!fu_efi_file_path_device_path_set_name(dp_file, name_with_root, error))
		return NULL;
	fu_firmware_add_image(FU_FIRMWARE(dp_buf), FU_FIRMWARE(dp_hd));
	fu_firmware_add_image(FU_FIRMWARE(dp_buf), FU_FIRMWARE(dp_file));
	return g_steal_pointer(&dp_buf);
}

GBytes *
fu_uefi_device_fixup_firmware(FuUefiDevice *self, GBytes *fw, GError **error)
{
	FuUefiDevicePrivate *priv = GET_PRIVATE(self);
	fwupd_guid_t esrt_guid = {0x0};
	guint hdrsize = getpagesize();
	gsize bufsz;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);
	g_autofree gchar *guid_new = NULL;
	g_autoptr(GByteArray) st_cap = fu_struct_efi_capsule_header_new();

	priv->missing_header = FALSE;

	/* GUID is the first 16 bytes */
	if (bufsz < sizeof(fwupd_guid_t)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "Invalid payload");
		return NULL;
	}
	guid_new = fwupd_guid_to_string((fwupd_guid_t *)buf, FWUPD_GUID_FLAG_MIXED_ENDIAN);

	/* ESRT header matches payload */
	if (g_strcmp0(fu_uefi_device_get_guid(self), guid_new) == 0) {
		g_debug("ESRT matches payload GUID");
		return g_bytes_ref(fw);
	}
	if (g_strcmp0(guid_new, FU_EFI_FMP_CAPSULE_GUID) == 0 ||
	    fu_device_has_private_flag(FU_DEVICE(self),
				       FU_UEFI_DEVICE_FLAG_NO_CAPSULE_HEADER_FIXUP)) {
		return g_bytes_ref(fw);
	}

	/* create a fake header with plausible contents */
	g_info("missing or invalid embedded capsule header");
	priv->missing_header = TRUE;
	fu_struct_efi_capsule_header_set_flags(st_cap, priv->capsule_flags);
	fu_struct_efi_capsule_header_set_header_size(st_cap, hdrsize);
	fu_struct_efi_capsule_header_set_image_size(st_cap, bufsz + hdrsize);
	if (!fwupd_guid_from_string(fu_uefi_device_get_guid(self),
				    &esrt_guid,
				    FWUPD_GUID_FLAG_MIXED_ENDIAN,
				    error)) {
		g_prefix_error(error, "Invalid ESRT GUID: ");
		return NULL;
	}
	fu_struct_efi_capsule_header_set_guid(st_cap, &esrt_guid);

	/* pad to the headersize then add the payload */
	fu_byte_array_set_size(st_cap, hdrsize, 0x00);
	g_byte_array_append(st_cap, buf, bufsz);
	return g_bytes_new(st_cap->data, st_cap->len);
}

gboolean
fu_uefi_device_write_update_info(FuUefiDevice *self,
				 const gchar *capsule_path,
				 const gchar *varname,
				 const gchar *guid_str,
				 GError **error)
{
	FuUefiDevicePrivate *priv = GET_PRIVATE(self);
	fwupd_guid_t guid = {0x0};
	g_autoptr(FuEfiDevicePathList) dp_buf = NULL;
	g_autoptr(GBytes) dp_blob = NULL;
	g_autoptr(GByteArray) st_inf = fu_struct_efi_update_info_new();

	/* set the body as the device path */
	if (g_getenv("FWUPD_UEFI_TEST") != NULL) {
		g_debug("not building device path, in tests....");
		return TRUE;
	}

	/* convert to EFI device path */
	dp_buf = fu_uefi_device_build_dp_buf(priv->esp, capsule_path, error);
	if (dp_buf == NULL)
		return FALSE;
	dp_blob = fu_firmware_write(FU_FIRMWARE(dp_buf), error);
	if (dp_blob == NULL)
		return FALSE;

	/* save this header and body to the hardware */
	if (!fwupd_guid_from_string(guid_str, &guid, FWUPD_GUID_FLAG_MIXED_ENDIAN, error))
		return FALSE;
	fu_struct_efi_update_info_set_flags(st_inf, priv->capsule_flags);
	fu_struct_efi_update_info_set_hw_inst(st_inf, priv->fmp_hardware_instance);
	fu_struct_efi_update_info_set_status(st_inf, FU_UEFI_UPDATE_INFO_STATUS_ATTEMPT_UPDATE);
	fu_struct_efi_update_info_set_guid(st_inf, &guid);
	fu_byte_array_append_bytes(st_inf, dp_blob);
	return fu_efivar_set_data(FU_EFIVAR_GUID_FWUPDATE,
				  varname,
				  st_inf->data,
				  st_inf->len,
				  FU_EFIVAR_ATTR_NON_VOLATILE | FU_EFIVAR_ATTR_BOOTSERVICE_ACCESS |
				      FU_EFIVAR_ATTR_RUNTIME_ACCESS,
				  error);
}

static gboolean
fu_uefi_check_asset(FuDevice *device, GError **error)
{
	g_autofree gchar *source_app = fu_uefi_get_built_app_path("fwupd", error);
	if (source_app == NULL) {
		if (fu_efivar_secure_boot_enabled(NULL))
			g_prefix_error(error, "missing signed bootloader for secure boot: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_uefi_device_cleanup_esp(FuDevice *device, GError **error)
{
	FuUefiDevice *self = FU_UEFI_DEVICE(device);
	FuUefiDevicePrivate *priv = GET_PRIVATE(self);
	g_autofree gchar *esp_path = fu_volume_get_mount_point(priv->esp);
	g_autofree gchar *pattern = NULL;
	g_autoptr(GPtrArray) files = NULL;

	/* in case we call capsule install twice before reboot */
	if (fu_efivar_exists(FU_EFIVAR_GUID_EFI_GLOBAL, "BootNext"))
		return TRUE;

	/* delete any files matching the glob in the ESP */
	files = fu_path_get_files(esp_path, error);
	if (files == NULL)
		return FALSE;
	pattern = g_build_filename(esp_path, "EFI/*/fw/fwupd*.cap", NULL);
	for (guint i = 0; i < files->len; i++) {
		const gchar *fn = g_ptr_array_index(files, i);
		if (g_pattern_match_simple(pattern, fn)) {
			g_autoptr(GFile) file = g_file_new_for_path(fn);
			g_debug("deleting %s", fn);
			if (!g_file_delete(file, NULL, error))
				return FALSE;
		}
	}

	/* delete any old variables */
	if (!fu_efivar_delete_with_glob(FU_EFIVAR_GUID_FWUPDATE, "fwupd*-*", error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_uefi_device_prepare(FuDevice *device,
		       FuProgress *progress,
		       FwupdInstallFlags flags,
		       GError **error)
{
	FuUefiDevice *self = FU_UEFI_DEVICE(device);
	FuUefiDevicePrivate *priv = GET_PRIVATE(self);

	/* mount if required */
	priv->esp_locker = fu_volume_locker(priv->esp, error);
	if (priv->esp_locker == NULL)
		return FALSE;

	/* sanity checks */
	if (!fu_uefi_device_cleanup_esp(device, error))
		return FALSE;
	if (!fu_uefi_check_asset(device, error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_uefi_device_cleanup(FuDevice *device,
		       FuProgress *progress,
		       FwupdInstallFlags flags,
		       GError **error)
{
	FuUefiDevice *self = FU_UEFI_DEVICE(device);
	FuUefiDevicePrivate *priv = GET_PRIVATE(self);

	/* unmount ESP if we opened it */
	if (!fu_device_locker_close(priv->esp_locker, error))
		return FALSE;
	g_clear_object(&priv->esp_locker);

	return TRUE;
}

static gboolean
fu_uefi_device_probe(FuDevice *device, GError **error)
{
	FuUefiDevice *self = FU_UEFI_DEVICE(device);
	FuUefiDevicePrivate *priv = GET_PRIVATE(self);

	/* broken sysfs? */
	if (priv->fw_class == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "failed to read fw_class");
		return FALSE;
	}

	/* this is invalid */
	if (!fwupd_guid_is_valid(priv->fw_class)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "ESRT GUID '%s' was not valid",
			    priv->fw_class);
		return FALSE;
	}

	/* add GUID first, as quirks may set the version format */
	fu_device_add_guid(device, priv->fw_class);

	/* set versions */
	fu_device_set_version_from_uint32(device, priv->fw_version);
	if (priv->fw_version_lowest != 0) {
		g_autofree gchar *version_lowest =
		    fu_version_from_uint32(priv->fw_version_lowest,
					   fu_device_get_version_format(self));
		fu_device_set_version_lowest_raw(device, priv->fw_version_lowest);
		fu_device_set_version_lowest(device, version_lowest);
	}

	/* set flags */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_NEEDS_REBOOT);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_REQUIRE_AC);
	fu_device_add_internal_flag(device, FU_DEVICE_INTERNAL_FLAG_MD_SET_VERFMT);
	fu_device_add_internal_flag(device, FU_DEVICE_INTERNAL_FLAG_MD_SET_ICON);
	fu_device_add_internal_flag(device, FU_DEVICE_INTERNAL_FLAG_MD_SET_VENDOR);

	/* add icons */
	if (priv->kind == FU_UEFI_DEVICE_KIND_SYSTEM_FIRMWARE) {
		fu_device_add_icon(device, "computer");
		fu_device_add_instance_id(device, "main-system-firmware");
	}

	/* whether to create a missing header */
	if (priv->kind == FU_UEFI_DEVICE_KIND_FMP ||
	    priv->kind == FU_UEFI_DEVICE_KIND_DELL_TPM_FIRMWARE)
		fu_device_add_private_flag(device, FU_UEFI_DEVICE_FLAG_NO_CAPSULE_HEADER_FIXUP);

	/* success */
	return TRUE;
}

static void
fu_uefi_device_capture_efi_debugging(FuDevice *device)
{
	g_autofree gchar *str = NULL;
	g_autoptr(GBytes) buf = NULL;
	g_autoptr(GError) error_local = NULL;

	/* get the EFI variable contents */
	buf = fu_efivar_get_data_bytes(FU_EFIVAR_GUID_FWUPDATE,
				       "FWUPDATE_DEBUG_LOG",
				       NULL,
				       &error_local);
	if (buf == NULL) {
		fu_device_set_update_error(device, error_local->message);
		return;
	}

	/* convert from UCS-2 to UTF-8 */
	str = fu_utf16_to_utf8_bytes(buf, &error_local);
	if (str == NULL) {
		fu_device_set_update_error(device, error_local->message);
		return;
	}

	/* success */
	fu_device_set_update_error(device, str);
}

gboolean
fu_uefi_device_perhaps_enable_debugging(FuUefiDevice *self, GError **error)
{
	if (fu_device_has_private_flag(FU_DEVICE(self), FU_UEFI_DEVICE_FLAG_ENABLE_EFI_DEBUGGING)) {
		const guint8 data = 1;
		if (!fu_efivar_set_data(FU_EFIVAR_GUID_FWUPDATE,
					"FWUPDATE_VERBOSE",
					&data,
					sizeof(data),
					FU_EFIVAR_ATTR_NON_VOLATILE |
					    FU_EFIVAR_ATTR_BOOTSERVICE_ACCESS |
					    FU_EFIVAR_ATTR_RUNTIME_ACCESS,
					error)) {
			g_prefix_error(error, "failed to enable debugging: ");
			return FALSE;
		}
		return TRUE;
	}

	/* unset this */
	if (fu_efivar_exists(FU_EFIVAR_GUID_FWUPDATE, "FWUPDATE_VERBOSE")) {
		if (!fu_efivar_delete(FU_EFIVAR_GUID_FWUPDATE, "FWUPDATE_VERBOSE", error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_uefi_device_get_results(FuDevice *device, GError **error)
{
	FuUefiDevice *self = FU_UEFI_DEVICE(device);
	FuUefiDevicePrivate *priv = GET_PRIVATE(self);

	/* capture EFI binary debug output */
	if (fu_device_has_private_flag(device, FU_UEFI_DEVICE_FLAG_ENABLE_EFI_DEBUGGING))
		fu_uefi_device_capture_efi_debugging(device);

	/* just set the update error */
	fu_uefi_device_set_status(self, priv->last_attempt_status);
	return TRUE;
}

FuVolume *
fu_uefi_device_get_esp(FuUefiDevice *self)
{
	FuUefiDevicePrivate *priv = GET_PRIVATE(self);
	return priv->esp;
}

static FuFirmware *
fu_uefi_device_prepare_firmware(FuDevice *device,
				GBytes *fw,
				FwupdInstallFlags flags,
				GError **error)
{
	FuUefiDevice *self = FU_UEFI_DEVICE(device);
	FuUefiDevicePrivate *priv = GET_PRIVATE(self);
	gsize sz_reqd = priv->require_esp_free_space;

	/* check there is enough space in the ESP */
	if (sz_reqd == 0) {
		g_info("required ESP free space is not configured, using 2 x %uMB + 20MB",
		       (guint)g_bytes_get_size(fw) / (1024 * 1024));
		sz_reqd = g_bytes_get_size(fw) * 2 + (20u * 1024 * 1024);
	}
	if (!fu_volume_check_free_space(priv->esp, sz_reqd, error))
		return NULL;

	/* success */
	return fu_firmware_new_from_bytes(fw);
}

static void
fu_uefi_device_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	FuUefiDevice *self = FU_UEFI_DEVICE(object);
	FuUefiDevicePrivate *priv = GET_PRIVATE(self);
	switch (prop_id) {
	case PROP_FW_CLASS:
		priv->fw_class = g_value_dup_string(value);
		break;
	case PROP_KIND:
		priv->kind = g_value_get_uint(value);
		break;
	case PROP_CAPSULE_FLAGS:
		priv->capsule_flags = g_value_get_uint(value);
		break;
	case PROP_FW_VERSION:
		priv->fw_version = g_value_get_uint(value);
		break;
	case PROP_FW_VERSION_LOWEST:
		priv->fw_version_lowest = g_value_get_uint(value);
		break;
	case PROP_LAST_ATTEMPT_STATUS:
		fu_uefi_device_set_status(self, g_value_get_uint(value));
		break;
	case PROP_LAST_ATTEMPT_VERSION:
		priv->last_attempt_version = g_value_get_uint(value);
		break;
	case PROP_FMP_HARDWARE_INSTANCE:
		priv->fmp_hardware_instance = g_value_get_uint64(value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fu_uefi_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static void
fu_uefi_device_init(FuUefiDevice *self)
{
	fu_device_set_summary(FU_DEVICE(self), "UEFI ESRT device");
	fu_device_add_protocol(FU_DEVICE(self), "org.uefi.capsule");
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_MD_SET_SIGNED);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_MD_SET_FLAGS);
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_UEFI_DEVICE_FLAG_NO_UX_CAPSULE,
					"no-ux-capsule");
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_UEFI_DEVICE_FLAG_USE_SHIM_UNIQUE,
					"use-shim-unique");
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_UEFI_DEVICE_FLAG_USE_LEGACY_BOOTMGR_DESC,
					"use-legacy-bootmgr-desc");
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_UEFI_DEVICE_FLAG_SUPPORTS_BOOT_ORDER_LOCK,
					"supports-boot-order-lock");
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_UEFI_DEVICE_FLAG_USE_SHIM_FOR_SB,
					"use-shim-for-sb");
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_UEFI_DEVICE_FLAG_NO_RT_SET_VARIABLE,
					"no-rt-set-variable");
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_UEFI_DEVICE_FLAG_NO_CAPSULE_HEADER_FIXUP,
					"no-capsule-header-fixup");
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_UEFI_DEVICE_FLAG_ENABLE_EFI_DEBUGGING,
					"enable-debugging");
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_UEFI_DEVICE_FLAG_COD_INDEXED_FILENAME,
					"cod-indexed-filename");
}

static void
fu_uefi_device_finalize(GObject *object)
{
	FuUefiDevice *self = FU_UEFI_DEVICE(object);
	FuUefiDevicePrivate *priv = GET_PRIVATE(self);

	g_free(priv->fw_class);
	if (priv->esp != NULL)
		g_object_unref(priv->esp);
	if (priv->esp_locker != NULL)
		g_object_unref(priv->esp_locker);

	G_OBJECT_CLASS(fu_uefi_device_parent_class)->finalize(object);
}

static void
fu_uefi_device_class_init(FuUefiDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	GParamSpec *pspec;
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);

	object_class->set_property = fu_uefi_device_set_property;
	object_class->finalize = fu_uefi_device_finalize;
	klass_device->to_string = fu_uefi_device_to_string;
	klass_device->probe = fu_uefi_device_probe;
	klass_device->prepare_firmware = fu_uefi_device_prepare_firmware;
	klass_device->prepare = fu_uefi_device_prepare;
	klass_device->cleanup = fu_uefi_device_cleanup;
	klass_device->report_metadata_pre = fu_uefi_device_report_metadata_pre;
	klass_device->report_metadata_post = fu_uefi_device_report_metadata_post;
	klass_device->get_results = fu_uefi_device_get_results;
	klass_device->set_progress = fu_uefi_device_set_progress;

	/**
	 * FuUefiDevice:fw-class:
	 *
	 * The firmware class, i.e. the ESRT GUID.
	 */
	pspec =
	    g_param_spec_string("fw-class",
				NULL,
				NULL,
				NULL,
				G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_FW_CLASS, pspec);

	/**
	 * FuUefiDevice:kind:
	 *
	 * The device kind.
	 */
	pspec = g_param_spec_uint("kind",
				  NULL,
				  NULL,
				  FU_UEFI_DEVICE_KIND_UNKNOWN,
				  FU_UEFI_DEVICE_KIND_LAST - 1,
				  FU_UEFI_DEVICE_KIND_UNKNOWN,
				  G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_KIND, pspec);

	/**
	 * FuUefiDevice:capsule-flags:
	 *
	 * The capsule flags to use for the update.
	 */
	pspec = g_param_spec_uint("capsule-flags",
				  NULL,
				  NULL,
				  0,
				  G_MAXUINT32,
				  0,
				  G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_CAPSULE_FLAGS, pspec);

	/**
	 * FuUefiDevice:fw-version:
	 *
	 * The current firmware version.
	 */
	pspec = g_param_spec_uint("fw-version",
				  NULL,
				  NULL,
				  0,
				  G_MAXUINT32,
				  0,
				  G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_FW_VERSION, pspec);

	/**
	 * FuUefiDevice:fw-version-lowest:
	 *
	 * The lowest possible installable version.
	 */
	pspec = g_param_spec_uint("fw-version-lowest",
				  NULL,
				  NULL,
				  0,
				  G_MAXUINT32,
				  0,
				  G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_FW_VERSION_LOWEST, pspec);

	/**
	 * FuUefiDevice:last-attempt-status:
	 *
	 * The last attempt status value.
	 */
	pspec = g_param_spec_uint("last-attempt-status",
				  NULL,
				  NULL,
				  FU_UEFI_DEVICE_STATUS_SUCCESS,
				  FU_UEFI_DEVICE_STATUS_LAST - 1,
				  FU_UEFI_DEVICE_STATUS_SUCCESS,
				  G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_LAST_ATTEMPT_STATUS, pspec);

	/**
	 * FuUefiDevice:last-attempt-version:
	 *
	 * The last attempt firmware version.
	 */
	pspec = g_param_spec_uint("last-attempt-version",
				  NULL,
				  NULL,
				  0,
				  G_MAXUINT32,
				  0,
				  G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_LAST_ATTEMPT_VERSION, pspec);

	/**
	 * FuUefiDevice:fmp-hardware-instance:
	 *
	 * The FMP hardware instance.
	 */
	pspec =
	    g_param_spec_uint64("fmp-hardware-instance",
				NULL,
				NULL,
				0,
				G_MAXUINT64,
				0,
				G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_FMP_HARDWARE_INSTANCE, pspec);
}
