/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuEfiDevicePath"

#include "config.h"

#include "fu-byte-array.h"
#include "fu-bytes.h"
#include "fu-efi-hard-drive-device-path.h"
#include "fu-efi-struct.h"
#include "fu-firmware-common.h"
#include "fu-mem.h"
#include "fu-string.h"

/**
 * FuEfiHardDriveDevicePath:
 *
 * See also: [class@FuEfiDevicePath]
 */

struct _FuEfiHardDriveDevicePath {
	FuEfiDevicePath parent_instance;
	guint32 partition_number;
	guint64 partition_start; /* blocks */
	guint64 partition_size;	 /* blocks */
	fwupd_guid_t partition_signature;
	FuEfiHardDriveDevicePathPartitionFormat partition_format;
	FuEfiHardDriveDevicePathSignatureType signature_type;
};

G_DEFINE_TYPE(FuEfiHardDriveDevicePath, fu_efi_hard_drive_device_path, FU_TYPE_EFI_DEVICE_PATH)

/* Linux always considers sectors to be 512 bytes */
#define BLOCK_SIZE 0x200

static void
fu_efi_hard_drive_device_path_export(FuFirmware *firmware,
				     FuFirmwareExportFlags flags,
				     XbBuilderNode *bn)
{
	FuEfiHardDriveDevicePath *self = FU_EFI_HARD_DRIVE_DEVICE_PATH(firmware);
	g_autofree gchar *partition_signature =
	    fwupd_guid_to_string(&self->partition_signature, FWUPD_GUID_FLAG_MIXED_ENDIAN);
	fu_xmlb_builder_insert_kx(bn, "partition_number", self->partition_number);
	fu_xmlb_builder_insert_kx(bn, "partition_start", self->partition_start);
	fu_xmlb_builder_insert_kx(bn, "partition_size", self->partition_size);
	fu_xmlb_builder_insert_kv(bn, "partition_signature", partition_signature);
	fu_xmlb_builder_insert_kv(
	    bn,
	    "partition_format",
	    fu_efi_hard_drive_device_path_partition_format_to_string(self->partition_format));
	fu_xmlb_builder_insert_kv(
	    bn,
	    "signature_type",
	    fu_efi_hard_drive_device_path_signature_type_to_string(self->signature_type));
}

static gboolean
fu_efi_hard_drive_device_path_parse(FuFirmware *firmware,
				    GBytes *fw,
				    gsize offset,
				    FwupdInstallFlags flags,
				    GError **error)
{
	FuEfiHardDriveDevicePath *self = FU_EFI_HARD_DRIVE_DEVICE_PATH(firmware);
	gsize bufsz = 0;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);
	g_autoptr(GByteArray) st = NULL;

	/* re-parse */
	st = fu_struct_efi_hard_drive_device_path_parse(buf, bufsz, offset, error);
	if (st == NULL)
		return FALSE;
	self->partition_number = fu_struct_efi_hard_drive_device_path_get_partition_number(st);
	self->partition_start = fu_struct_efi_hard_drive_device_path_get_partition_start(st);
	self->partition_size = fu_struct_efi_hard_drive_device_path_get_partition_size(st);
	memcpy(self->partition_signature,
	       fu_struct_efi_hard_drive_device_path_get_partition_signature(st),
	       sizeof(self->partition_signature));
	self->partition_format = fu_struct_efi_hard_drive_device_path_get_partition_format(st);
	self->signature_type = fu_struct_efi_hard_drive_device_path_get_signature_type(st);

	/* success */
	fu_firmware_set_size(firmware, fu_struct_efi_device_path_get_length(st));
	return TRUE;
}

static GByteArray *
fu_efi_hard_drive_device_path_write(FuFirmware *firmware, GError **error)
{
	FuEfiHardDriveDevicePath *self = FU_EFI_HARD_DRIVE_DEVICE_PATH(firmware);
	g_autoptr(GByteArray) st = fu_struct_efi_hard_drive_device_path_new();

	/* required */
	fu_struct_efi_hard_drive_device_path_set_partition_number(st, self->partition_number);
	fu_struct_efi_hard_drive_device_path_set_partition_start(st, self->partition_start);
	fu_struct_efi_hard_drive_device_path_set_partition_size(st, self->partition_size);
	fu_struct_efi_hard_drive_device_path_set_partition_signature(st,
								     &self->partition_signature);
	fu_struct_efi_hard_drive_device_path_set_partition_format(st, self->partition_format);
	fu_struct_efi_hard_drive_device_path_set_signature_type(st, self->signature_type);

	/* success */
	return g_steal_pointer(&st);
}

static gboolean
fu_efi_hard_drive_device_path_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuEfiHardDriveDevicePath *self = FU_EFI_HARD_DRIVE_DEVICE_PATH(firmware);
	const gchar *tmp;
	guint64 value = 0;

	/* optional data */
	tmp = xb_node_query_text(n, "partition_number", NULL);
	if (tmp != NULL) {
		if (!fu_strtoull(tmp, &value, 0x0, G_MAXUINT32, error))
			return FALSE;
		self->partition_number = value;
	}
	tmp = xb_node_query_text(n, "partition_start", NULL);
	if (tmp != NULL) {
		if (!fu_strtoull(tmp, &value, 0x0, G_MAXUINT64, error))
			return FALSE;
		self->partition_start = value;
	}
	tmp = xb_node_query_text(n, "partition_size", NULL);
	if (tmp != NULL) {
		if (!fu_strtoull(tmp, &value, 0x0, G_MAXUINT64, error))
			return FALSE;
		self->partition_size = value;
	}
	tmp = xb_node_query_text(n, "partition_signature", NULL);
	if (tmp != NULL) {
		if (!fwupd_guid_from_string(tmp,
					    &self->partition_signature,
					    FWUPD_GUID_FLAG_MIXED_ENDIAN,
					    error))
			return FALSE;
	}
	tmp = xb_node_query_text(n, "partition_format", NULL);
	if (tmp != NULL) {
		self->partition_format =
		    fu_efi_hard_drive_device_path_partition_format_from_string(tmp);
	}
	tmp = xb_node_query_text(n, "signature_type", NULL);
	if (tmp != NULL) {
		self->signature_type =
		    fu_efi_hard_drive_device_path_signature_type_from_string(tmp);
	}

	/* success */
	return TRUE;
}

static void
fu_efi_hard_drive_device_path_init(FuEfiHardDriveDevicePath *self)
{
	fu_firmware_set_idx(FU_FIRMWARE(self), FU_EFI_DEVICE_PATH_TYPE_MEDIA);
	fu_efi_device_path_set_subtype(FU_EFI_DEVICE_PATH(self),
				       FU_EFI_HARD_DRIVE_DEVICE_PATH_SUBTYPE_HARD_DRIVE);
}

static void
fu_efi_hard_drive_device_path_class_init(FuEfiHardDriveDevicePathClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->export = fu_efi_hard_drive_device_path_export;
	klass_firmware->parse = fu_efi_hard_drive_device_path_parse;
	klass_firmware->write = fu_efi_hard_drive_device_path_write;
	klass_firmware->build = fu_efi_hard_drive_device_path_build;
}

/**
 * fu_efi_hard_drive_device_path_new:
 *
 * Creates a new EFI `DEVICE_PATH`.
 *
 * Returns: (transfer full): a #FuEfiHardDriveDevicePath
 *
 * Since: 1.9.3
 **/
FuEfiHardDriveDevicePath *
fu_efi_hard_drive_device_path_new(void)
{
	return g_object_new(FU_TYPE_EFI_HARD_DRIVE_DEVICE_PATH, NULL);
}

/**
 * fu_efi_hard_drive_device_path_new_from_volume:
 * @volume: a #FuVolume
 * @error: (nullable): optional return location for an error
 *
 * Creates a new EFI `DEVICE_PATH` for a specific volume.
 *
 * Returns: (transfer full): a #FuEfiHardDriveDevicePath, or %NULL on error
 *
 * Since: 1.9.3
 **/
FuEfiHardDriveDevicePath *
fu_efi_hard_drive_device_path_new_from_volume(FuVolume *volume, GError **error)
{
	g_autoptr(FuEfiHardDriveDevicePath) self = fu_efi_hard_drive_device_path_new();
	g_autofree gchar *partition_uuid = NULL;
	g_autofree gchar *partition_kind = NULL;

	g_return_val_if_fail(FU_IS_VOLUME(volume), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* common to both */
	self->partition_number = fu_volume_get_partition_number(volume);
	self->partition_start = fu_volume_get_partition_offset(volume) / BLOCK_SIZE;
	self->partition_size = fu_volume_get_partition_size(volume) / BLOCK_SIZE;

	/* set up the rest of the struct */
	partition_kind = fu_volume_get_partition_kind(volume);
	partition_uuid = fu_volume_get_partition_uuid(volume);
	if (partition_uuid == NULL) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_SUPPORTED,
				    "partition UUID required");
		return NULL;
	}
	if (g_strcmp0(partition_kind, FU_VOLUME_KIND_ESP) == 0 ||
	    g_strcmp0(partition_kind, FU_VOLUME_KIND_BDP) == 0) {
		self->partition_format =
		    FU_EFI_HARD_DRIVE_DEVICE_PATH_PARTITION_FORMAT_GUID_PARTITION_TABLE;
		self->signature_type = FU_EFI_HARD_DRIVE_DEVICE_PATH_SIGNATURE_TYPE_GUID;
		if (!fwupd_guid_from_string(partition_uuid,
					    &self->partition_signature,
					    FWUPD_GUID_FLAG_MIXED_ENDIAN,
					    error))
			return NULL;
	} else if (g_strcmp0(partition_kind, "0xef") == 0) {
		guint32 value = 0;
		g_auto(GStrv) parts = g_strsplit(partition_uuid, "-", -1);
		if (!fu_firmware_strparse_uint32_safe(parts[0],
						      strlen(parts[0]),
						      0x0,
						      &value,
						      error)) {
			g_prefix_error(error, "failed to parse %s: ", parts[0]);
			return NULL;
		}
		if (!fu_memwrite_uint32_safe(self->partition_signature,
					     sizeof(self->partition_signature),
					     0x0,
					     value,
					     G_LITTLE_ENDIAN,
					     error))
			return NULL;
		self->partition_format = FU_EFI_HARD_DRIVE_DEVICE_PATH_PARTITION_FORMAT_LEGACY_MBR;
		self->signature_type = FU_EFI_HARD_DRIVE_DEVICE_PATH_SIGNATURE_TYPE_ADDR1B8;
	} else {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "partition kind %s not supported",
			    partition_kind);
		return NULL;
	}

	/* success */
	return g_steal_pointer(&self);
}
