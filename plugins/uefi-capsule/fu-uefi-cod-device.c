/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <string.h>

#include "fu-uefi-cod-device.h"
#include "fu-uefi-common.h"

struct _FuUefiCodDevice {
	FuUefiCapsuleDevice parent_instance;
};

G_DEFINE_TYPE(FuUefiCodDevice, fu_uefi_cod_device, FU_TYPE_UEFI_CAPSULE_DEVICE)

static gboolean
fu_uefi_cod_device_get_results_for_idx(FuDevice *device, guint idx, GError **error)
{
	FuContext *ctx = fu_device_get_context(device);
	FuEfivars *efivars = fu_context_get_efivars(ctx);
	FuUefiCapsuleDevice *device_uefi = FU_UEFI_CAPSULE_DEVICE(device);
	fwupd_guid_t guid = {0x0};
	gsize bufsz = 0;
	guint32 status = 0;
	guint32 total_size = 0;
	g_autofree gchar *guidstr = NULL;
	g_autofree gchar *name = NULL;
	g_autofree guint8 *buf = NULL;

	/* read out result */
	name = g_strdup_printf("Capsule%04u", idx);
	if (!fu_efivars_get_data(efivars,
				 FU_EFIVARS_GUID_EFI_CAPSULE_REPORT,
				 name,
				 &buf,
				 &bufsz,
				 NULL,
				 error)) {
		g_prefix_error(error, "failed to read %s: ", name);
		return FALSE;
	}

	/* sanity check */
	if (!fu_memread_uint32_safe(buf, bufsz, 0x00, &total_size, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (total_size < 0x3A) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "EFI_CAPSULE_RESULT_VARIABLE_HEADER too small");
		return FALSE;
	}

	/* verify guid */
	if (!fu_memcpy_safe(guid,
			    sizeof(guid),
			    0x0, /* dst */
			    buf,
			    bufsz,
			    0x08, /* src */
			    sizeof(guid),
			    error))
		return FALSE;
	guidstr = fwupd_guid_to_string(&guid, FWUPD_GUID_FLAG_MIXED_ENDIAN);
	if (g_strcmp0(guidstr, fu_uefi_capsule_device_get_guid(device_uefi)) != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "wrong GUID, expected %s, got %s",
			    fu_uefi_capsule_device_get_guid(device_uefi),
			    guidstr);
		return FALSE;
	}

	/* get status */
	if (!fu_memread_uint32_safe(buf, bufsz, 0x28, &status, G_LITTLE_ENDIAN, error))
		return FALSE;
	fu_uefi_capsule_device_set_status(device_uefi, status);
	return TRUE;
}

#define VARIABLE_IDX_SIZE 11 /* of CHAR16 */

static gboolean
fu_uefi_cod_device_get_variable_idx(FuUefiCodDevice *self,
				    const gchar *name,
				    guint *value,
				    GError **error)
{
	FuContext *ctx = fu_device_get_context(FU_DEVICE(self));
	FuEfivars *efivars = fu_context_get_efivars(ctx);
	guint64 tmp = 0;
	g_autofree gchar *str = NULL;
	g_autoptr(GBytes) buf = NULL;

	/* parse the value */
	buf = fu_efivars_get_data_bytes(efivars,
					FU_EFIVARS_GUID_EFI_CAPSULE_REPORT,
					name,
					NULL,
					error);
	if (buf == NULL)
		return FALSE;
	str = fu_utf16_to_utf8_bytes(buf, G_LITTLE_ENDIAN, error);
	if (str == NULL)
		return FALSE;
	if (!g_str_has_prefix(str, "Capsule")) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "wrong contents, got '%s' for %s",
			    str,
			    name);
		return FALSE;
	}
	if (!fu_strtoull(str + strlen("Capsule"),
			 &tmp,
			 0,
			 G_MAXUINT32,
			 FU_INTEGER_BASE_AUTO,
			 error))
		return FALSE;
	if (value != NULL)
		*value = tmp;
	return TRUE;
}

static gboolean
fu_uefi_cod_device_get_results(FuDevice *device, GError **error)
{
	FuUefiCodDevice *self = FU_UEFI_COD_DEVICE(device);
	guint capsule_last = 1024;

	/* tell us where to stop */
	if (!fu_uefi_cod_device_get_variable_idx(self, "CapsuleLast", &capsule_last, error))
		return FALSE;
	for (guint i = 0; i <= capsule_last; i++) {
		g_autoptr(GError) error_local = NULL;
		if (fu_uefi_cod_device_get_results_for_idx(device, i, &error_local))
			return TRUE;
		if (!g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND)) {
			g_propagate_error(error, g_steal_pointer(&error_local));
			return FALSE;
		}
	}

	/* nothing found */
	return TRUE;
}

static gchar *
fu_uefi_cod_device_get_indexed_filename(FuUefiCapsuleDevice *self, GError **error)
{
	g_autofree gchar *esp_path =
	    fu_volume_get_mount_point(fu_uefi_capsule_device_get_esp(self));
	for (guint i = 0; i < 0xFFFF; i++) {
		gboolean exists_cod_path = FALSE;
		g_autofree gchar *basename = g_strdup_printf("CapsuleUpdateFile%04X.bin", i);
		g_autofree gchar *cod_path =
		    g_build_filename(esp_path, "EFI", "UpdateCapsule", basename, NULL);
		if (!fu_device_query_file_exists(FU_DEVICE(self),
						 cod_path,
						 &exists_cod_path,
						 error))
			return NULL;
		if (!exists_cod_path)
			return g_steal_pointer(&cod_path);
	}
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "all potential CapsuleUpdateFile file names are taken");
	return NULL;
}

static gchar *
fu_uefi_cod_device_get_filename(FuUefiCapsuleDevice *self, GError **error)
{
	FuVolume *esp = fu_uefi_capsule_device_get_esp(self);
	g_autofree gchar *esp_path = NULL;
	g_autofree gchar *basename = NULL;

	if (esp == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "no ESP set on device");
		return NULL;
	}
	esp_path = fu_volume_get_mount_point(esp);

	/* InsydeH2O */
	if (fu_device_has_private_flag(FU_DEVICE(self),
				       FU_UEFI_CAPSULE_DEVICE_FLAG_COD_INDEXED_FILENAME))
		return fu_uefi_cod_device_get_indexed_filename(self, error);

	/* Dell Inc. */
	if (fu_device_has_private_flag(FU_DEVICE(self),
				       FU_UEFI_CAPSULE_DEVICE_FLAG_COD_DELL_RECOVERY)) {
		return g_build_filename(esp_path,
					"EFI",
					"dell",
					"bios",
					"recovery",
					"BIOS_TRS.rcv",
					NULL);
	}

	/* fallback */
	basename = g_strdup_printf("fwupd-%s.cap", fu_uefi_capsule_device_get_guid(self));
	return g_build_filename(esp_path, "EFI", "UpdateCapsule", basename, NULL);
}

static gboolean
fu_uefi_cod_device_write_firmware(FuDevice *device,
				  FuFirmware *firmware,
				  FuProgress *progress,
				  FwupdInstallFlags flags,
				  GError **error)
{
	FuContext *ctx = fu_device_get_context(device);
	FuEfivars *efivars = fu_context_get_efivars(ctx);
	FuUefiCapsuleDevice *self = FU_UEFI_CAPSULE_DEVICE(device);
	g_autofree gchar *cod_path = NULL;
	g_autoptr(GBytes) fixed_fw = NULL;
	g_autoptr(GBytes) fw = NULL;

	/* ensure we have the existing state */
	if (fu_uefi_capsule_device_get_guid(self) == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "cannot update device info with no GUID");
		return FALSE;
	}

	/* copy the capsule */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;
	fixed_fw = fu_uefi_capsule_device_fixup_firmware(self, fw, error);
	if (fixed_fw == NULL)
		return FALSE;
	cod_path = fu_uefi_cod_device_get_filename(self, error);
	if (cod_path == NULL)
		return FALSE;
	g_info("using %s", cod_path);
	if (!fu_path_mkdir_parent(cod_path, error))
		return FALSE;
	if (!fu_bytes_set_contents(cod_path, fixed_fw, error))
		return FALSE;

	/*
	 * NOTE: The EFI spec requires setting OsIndications!
	 * RT->SetVariable is not supported for all hardware, and so when using
	 * U-Boot, it applies the capsule even if OsIndications isn't set.
	 * The capsule is then deleted by U-Boot after it has been deployed.
	 */
	if (!fu_device_has_private_flag(device, FU_UEFI_CAPSULE_DEVICE_FLAG_NO_RT_SET_VARIABLE)) {
		gsize bufsz = 0;
		guint64 os_indications = 0;
		g_autofree guint8 *buf = NULL;
		g_autoptr(GError) error_local = NULL;

		/* the firmware does not normally populate OsIndications by default */
		if (!fu_efivars_get_data(efivars,
					 FU_EFIVARS_GUID_EFI_GLOBAL,
					 "OsIndications",
					 &buf,
					 &bufsz,
					 NULL,
					 &error_local)) {
			g_debug("failed to read EFI variable: %s", error_local->message);
		} else {
			if (!fu_memread_uint64_safe(buf,
						    bufsz,
						    0x0,
						    &os_indications,
						    G_LITTLE_ENDIAN,
						    error))
				return FALSE;
		}
		os_indications |= EFI_OS_INDICATIONS_FILE_CAPSULE_DELIVERY_SUPPORTED;
		if (!fu_efivars_set_data(efivars,
					 FU_EFIVARS_GUID_EFI_GLOBAL,
					 "OsIndications",
					 (guint8 *)&os_indications,
					 sizeof(os_indications),
					 FU_EFIVARS_ATTR_NON_VOLATILE |
					     FU_EFIVARS_ATTR_BOOTSERVICE_ACCESS |
					     FU_EFIVARS_ATTR_RUNTIME_ACCESS,
					 error)) {
			g_prefix_error(error, "Could not set OsIndications: ");
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static void
fu_uefi_cod_device_report_metadata_pre(FuDevice *device, GHashTable *metadata)
{
	/* FuUefiCapsuleDevice */
	FU_DEVICE_CLASS(fu_uefi_cod_device_parent_class)->report_metadata_pre(device, metadata);
	g_hash_table_insert(metadata, g_strdup("CapsuleApplyMethod"), g_strdup("cod"));
}

static void
fu_uefi_cod_device_init(FuUefiCodDevice *self)
{
	fu_device_add_private_flag(FU_DEVICE(self), FU_UEFI_CAPSULE_DEVICE_FLAG_NO_UX_CAPSULE);
	fu_device_set_summary(FU_DEVICE(self),
			      "UEFI System Resource Table device (Updated via capsule-on-disk)");
}

static void
fu_uefi_cod_device_class_init(FuUefiCodDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->write_firmware = fu_uefi_cod_device_write_firmware;
	device_class->get_results = fu_uefi_cod_device_get_results;
	device_class->report_metadata_pre = fu_uefi_cod_device_report_metadata_pre;
}
