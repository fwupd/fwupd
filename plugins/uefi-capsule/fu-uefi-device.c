/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2015 Peter Jones <pjones@redhat.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include <efivar.h>
#include <efivar/efiboot.h>
#include <string.h>

#include "fu-uefi-common.h"
#include "fu-uefi-device.h"
#include "fu-uefi-devpath.h"

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
} FuUefiDevicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuUefiDevice, fu_uefi_device, FU_TYPE_DEVICE)

#define GET_PRIVATE(o) (fu_uefi_device_get_instance_private(o))

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

const gchar *
fu_uefi_device_kind_to_string(FuUefiDeviceKind kind)
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

FuUefiDeviceKind
fu_uefi_device_kind_from_string(const gchar *kind)
{
	if (g_strcmp0(kind, "system-firmware") == 0)
		return FU_UEFI_DEVICE_KIND_SYSTEM_FIRMWARE;
	if (g_strcmp0(kind, "device-firmware") == 0)
		return FU_UEFI_DEVICE_KIND_DEVICE_FIRMWARE;
	if (g_strcmp0(kind, "uefi-driver") == 0)
		return FU_UEFI_DEVICE_KIND_UEFI_DRIVER;
	if (g_strcmp0(kind, "fmp") == 0)
		return FU_UEFI_DEVICE_KIND_FMP;
	if (g_strcmp0(kind, "dell-tpm-firmware") == 0)
		return FU_UEFI_DEVICE_KIND_DELL_TPM_FIRMWARE;
	return FU_UEFI_DEVICE_KIND_UNKNOWN;
}

const gchar *
fu_uefi_device_status_to_string(FuUefiDeviceStatus status)
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
fu_uefi_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuUefiDevice *self = FU_UEFI_DEVICE(device);
	FuUefiDevicePrivate *priv = GET_PRIVATE(self);
	fu_common_string_append_kv(str, idt, "Kind", fu_uefi_device_kind_to_string(priv->kind));
	fu_common_string_append_kv(str, idt, "FwClass", priv->fw_class);
	fu_common_string_append_kx(str, idt, "CapsuleFlags", priv->capsule_flags);
	fu_common_string_append_kx(str, idt, "FwVersion", priv->fw_version);
	fu_common_string_append_kx(str, idt, "FwVersionLowest", priv->fw_version_lowest);
	fu_common_string_append_kv(str,
				   idt,
				   "LastAttemptStatus",
				   fu_uefi_device_status_to_string(priv->last_attempt_status));
	fu_common_string_append_kx(str, idt, "LastAttemptVersion", priv->last_attempt_version);
	if (priv->esp != NULL) {
		fu_common_string_append_kv(str, idt, "EspId", fu_volume_get_id(priv->esp));
	}
	fu_common_string_append_ku(str,
				   idt,
				   "RequireESPFreeSpace",
				   fu_device_get_metadata_integer(device, "RequireESPFreeSpace"));
}

static void
fu_uefi_device_report_metadata_pre(FuDevice *device, GHashTable *metadata)
{
	FuUefiDevice *self = FU_UEFI_DEVICE(device);
	FuUefiDevicePrivate *priv = GET_PRIVATE(self);

	/* record if we had an invalid header during update */
	g_hash_table_insert(metadata,
			    g_strdup("MissingCapsuleHeader"),
			    g_strdup(priv->missing_header ? "True" : "False"));

	/* where the ESP was mounted during installation */
	g_hash_table_insert(metadata, g_strdup("EspPath"), fu_volume_get_mount_point(priv->esp));
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
	gsize datasz = 0;
	g_autofree gchar *varname = fu_uefi_device_build_varname(self);
	g_autofree guint8 *data = NULL;
	g_autoptr(FuUefiUpdateInfo) info = fu_uefi_update_info_new();

	g_return_val_if_fail(FU_IS_UEFI_DEVICE(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* get the existing status */
	if (!fu_efivar_get_data(FU_EFIVAR_GUID_FWUPDATE, varname, &data, &datasz, NULL, error))
		return NULL;
	if (!fu_uefi_update_info_parse(info, data, datasz, error))
		return NULL;
	return g_steal_pointer(&info);
}

gboolean
fu_uefi_device_clear_status(FuUefiDevice *self, GError **error)
{
	efi_update_info_t info;
	gsize datasz = 0;
	g_autofree gchar *varname = fu_uefi_device_build_varname(self);
	g_autofree guint8 *data = NULL;

	g_return_val_if_fail(FU_IS_UEFI_DEVICE(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* get the existing status */
	if (!fu_efivar_get_data(FU_EFIVAR_GUID_FWUPDATE, varname, &data, &datasz, NULL, error))
		return FALSE;
	if (datasz < sizeof(efi_update_info_t)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "EFI variable is corrupt");
		return FALSE;
	}

	/* just copy the efi_update_info_t, ignore devpath then save it back */
	memcpy(&info, data, sizeof(info));
	info.status = FU_UEFI_DEVICE_STATUS_SUCCESS;
	memcpy(data, &info, sizeof(info));
	return fu_efivar_set_data(FU_EFIVAR_GUID_FWUPDATE,
				  varname,
				  data,
				  datasz,
				  FU_EFIVAR_ATTR_NON_VOLATILE | FU_EFIVAR_ATTR_BOOTSERVICE_ACCESS |
				      FU_EFIVAR_ATTR_RUNTIME_ACCESS,
				  error);
}

static guint8 *
fu_uefi_device_build_dp_buf(const gchar *path, gsize *bufsz, GError **error)
{
	gssize req;
	gssize sz;
	g_autofree guint8 *dp_buf = NULL;
	g_autoptr(GPtrArray) dps = NULL;

	/* get the size of the path first */
	req = efi_generate_file_device_path(NULL,
					    0,
					    path,
					    EFIBOOT_OPTIONS_IGNORE_FS_ERROR | EFIBOOT_ABBREV_HD);
	if (req < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "failed to efi_generate_file_device_path(%s)",
			    path);
		return NULL;
	}

	/* if we just have an end device path, it's not going to work */
	if (req <= 4) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "failed to get valid device_path for (%s)",
			    path);
		return NULL;
	}

	/* actually get the path this time */
	dp_buf = g_malloc0(req);
	sz = efi_generate_file_device_path(dp_buf,
					   req,
					   path,
					   EFIBOOT_OPTIONS_IGNORE_FS_ERROR | EFIBOOT_ABBREV_HD);
	if (sz < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "failed to efi_generate_file_device_path(%s)",
			    path);
		return NULL;
	}

	/* parse what we got back from efivar */
	dps = fu_uefi_devpath_parse(dp_buf, (gsize)sz, FU_UEFI_DEVPATH_PARSE_FLAG_NONE, error);
	if (dps == NULL) {
		fu_common_dump_raw(G_LOG_DOMAIN, "dp_buf", dp_buf, (gsize)sz);
		return NULL;
	}

	/* success */
	if (bufsz != NULL)
		*bufsz = sz;
	return g_steal_pointer(&dp_buf);
}

GBytes *
fu_uefi_device_fixup_firmware(FuUefiDevice *self, GBytes *fw, GError **error)
{
	FuUefiDevicePrivate *priv = GET_PRIVATE(self);
	gsize bufsz;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);
	g_autofree gchar *guid_new = NULL;

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
	} else if (fu_device_has_private_flag(FU_DEVICE(self),
					      FU_UEFI_DEVICE_FLAG_NO_CAPSULE_HEADER_FIXUP)) {
		return g_bytes_ref(fw);
	} else {
		guint hdrsize = getpagesize();
		fwupd_guid_t esrt_guid = {0x0};
		efi_capsule_header_t header = {0x0};
		g_autoptr(GByteArray) buf_hdr = g_byte_array_new();

		g_warning("missing or invalid embedded capsule header");
		priv->missing_header = TRUE;

		/* create a fake header with plausible contents */
		header.flags = priv->capsule_flags;
		header.header_size = hdrsize;
		header.capsule_image_size = bufsz + hdrsize;
		if (!fwupd_guid_from_string(fu_uefi_device_get_guid(self),
					    &esrt_guid,
					    FWUPD_GUID_FLAG_MIXED_ENDIAN,
					    error)) {
			g_prefix_error(error, "Invalid ESRT GUID: ");
			return NULL;
		}
		memcpy(&header.guid, &esrt_guid, sizeof(fwupd_guid_t));

		/* prepend the header to the payload */
		g_byte_array_append(buf_hdr, (const guint8 *)&header, sizeof(header));
		fu_byte_array_set_size(buf_hdr, hdrsize);
		g_byte_array_append(buf_hdr, buf, bufsz);
		return g_byte_array_free_to_bytes(g_steal_pointer(&buf_hdr));
	}
}

gboolean
fu_uefi_device_write_update_info(FuUefiDevice *self,
				 const gchar *filename,
				 const gchar *varname,
				 const gchar *guid,
				 GError **error)
{
	FuUefiDevicePrivate *priv = GET_PRIVATE(self);
	gsize dp_bufsz = 0;
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autofree guint8 *dp_buf = NULL;
	efi_update_info_t info = {
	    .update_info_version = 0x7,
	    .guid = {0x0},
	    .capsule_flags = priv->capsule_flags,
	    .hw_inst = priv->fmp_hardware_instance,
	    .time_attempted = {0x0},
	    .status = FU_UEFI_UPDATE_INFO_STATUS_ATTEMPT_UPDATE,
	};

	/* set the body as the device path */
	if (g_getenv("FWUPD_UEFI_TEST") != NULL) {
		g_debug("not building device path, in tests....");
		return TRUE;
	}

	/* convert to EFI device path */
	dp_buf = fu_uefi_device_build_dp_buf(filename, &dp_bufsz, error);
	if (dp_buf == NULL)
		return FALSE;

	/* save this header and body to the hardware */
	if (!fwupd_guid_from_string(guid, &info.guid, FWUPD_GUID_FLAG_MIXED_ENDIAN, error))
		return FALSE;
	g_byte_array_append(buf, (const guint8 *)&info, sizeof(info));
	g_byte_array_append(buf, dp_buf, dp_bufsz);
	return fu_efivar_set_data(FU_EFIVAR_GUID_FWUPDATE,
				  varname,
				  buf->data,
				  buf->len,
				  FU_EFIVAR_ATTR_NON_VOLATILE | FU_EFIVAR_ATTR_BOOTSERVICE_ACCESS |
				      FU_EFIVAR_ATTR_RUNTIME_ACCESS,
				  error);
}

static gboolean
fu_uefi_device_check_esp_free(FuDevice *device, GError **error)
{
	FuUefiDevice *self = FU_UEFI_DEVICE(device);
	FuUefiDevicePrivate *priv = GET_PRIVATE(self);
	guint64 sz_reqd = fu_device_get_metadata_integer(device, "RequireESPFreeSpace");
	if (sz_reqd == G_MAXUINT) {
		g_debug("maximum size is not configured");
		return TRUE;
	}
	return fu_volume_check_free_space(priv->esp, sz_reqd, error);
}

static gboolean
fu_uefi_check_asset(FuDevice *device, GError **error)
{
	g_autofree gchar *source_app = fu_uefi_get_built_app_path(error);
	if (source_app == NULL) {
		if (fu_efivar_secure_boot_enabled())
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
	files = fu_common_get_files_recursive(esp_path, error);
	if (files == NULL)
		return FALSE;
	pattern = g_build_filename(esp_path, "EFI/*/fw/fwupd*.cap", NULL);
	for (guint i = 0; i < files->len; i++) {
		const gchar *fn = g_ptr_array_index(files, i);
		if (fu_common_fnmatch(pattern, fn)) {
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
	if (!fu_uefi_device_check_esp_free(device, error))
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
	FwupdVersionFormat version_format;
	g_autofree gchar *version_lowest = NULL;
	g_autofree gchar *version = NULL;

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
	version_format = fu_device_get_version_format(device);
	version = fu_common_version_from_uint32(priv->fw_version, version_format);
	fu_device_set_version_format(device, version_format);
	fu_device_set_version_raw(device, priv->fw_version);
	fu_device_set_version(device, version);
	if (priv->fw_version_lowest != 0) {
		version_lowest =
		    fu_common_version_from_uint32(priv->fw_version_lowest, version_format);
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
	if (priv->kind == FU_UEFI_DEVICE_KIND_DEVICE_FIRMWARE) {
		/* nothing better in the icon naming spec */
		fu_device_add_icon(device, "audio-card");
	} else {
		/* this is probably system firmware */
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

static gboolean
fu_uefi_device_get_results(FuDevice *device, GError **error)
{
	FuUefiDevice *self = FU_UEFI_DEVICE(device);
	FuUefiDevicePrivate *priv = GET_PRIVATE(self);

	/* just set the update error */
	fu_uefi_device_set_status(self, priv->last_attempt_status);
	return TRUE;
}

gchar *
fu_uefi_device_get_esp_path(FuUefiDevice *self)
{
	FuUefiDevicePrivate *priv = GET_PRIVATE(self);
	return fu_volume_get_mount_point(priv->esp);
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
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0); /* detach */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100); /* write */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0); /* attach */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0);	/* reload */
}

static void
fu_uefi_device_init(FuUefiDevice *self)
{
	fu_device_set_summary(FU_DEVICE(self), "UEFI ESRT device");
	fu_device_add_protocol(FU_DEVICE(self), "org.uefi.capsule");
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_MD_SET_SIGNED);
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
					FU_UEFI_DEVICE_FLAG_FALLBACK_TO_REMOVABLE_PATH,
					"fallback-to-removable-path");
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_UEFI_DEVICE_FLAG_NO_RT_SET_VARIABLE,
					"no-rt-set-variable");
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_UEFI_DEVICE_FLAG_NO_CAPSULE_HEADER_FIXUP,
					"no-capsule-header-fixup");
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
