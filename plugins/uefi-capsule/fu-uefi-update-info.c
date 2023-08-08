/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-uefi-common.h"
#include "fu-uefi-update-info.h"

#define EFIDP_MEDIA_TYPE 0x04
#define EFIDP_MEDIA_FILE 0x4

struct _FuUefiUpdateInfo {
	FuFirmware parent_instance;
	gchar *guid;
	gchar *capsule_fn;
	guint32 capsule_flags;
	guint64 hw_inst;
	FuUefiUpdateInfoStatus status;
};

G_DEFINE_TYPE(FuUefiUpdateInfo, fu_uefi_update_info, FU_TYPE_FIRMWARE)

static void
fu_uefi_update_info_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuUefiUpdateInfo *self = FU_UEFI_UPDATE_INFO(firmware);
	fu_xmlb_builder_insert_kv(bn, "guid", self->guid);
	fu_xmlb_builder_insert_kv(bn, "capsule_fn", self->capsule_fn);
	fu_xmlb_builder_insert_kx(bn, "capsule_flags", self->capsule_flags);
	fu_xmlb_builder_insert_kx(bn, "hw_inst", self->hw_inst);
	fu_xmlb_builder_insert_kv(bn, "status", fu_uefi_update_info_status_to_string(self->status));
}

static gboolean
fu_uefi_update_info_parse(FuFirmware *firmware,
			  GBytes *fw,
			  gsize offset,
			  FwupdInstallFlags flags,
			  GError **error)
{
	FuUefiUpdateInfo *self = FU_UEFI_UPDATE_INFO(firmware);
	gsize bufsz = 0;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);
	g_autoptr(GByteArray) st_inf = NULL;

	g_return_val_if_fail((self), FALSE);

	st_inf = fu_struct_efi_update_info_parse(buf, bufsz, 0x0, error);
	if (st_inf == NULL) {
		g_prefix_error(error, "EFI variable is corrupt: ");
		return FALSE;
	}
	fu_firmware_set_version_raw(firmware, fu_struct_efi_update_info_get_version(st_inf));
	self->capsule_flags = fu_struct_efi_update_info_get_flags(st_inf);
	self->hw_inst = fu_struct_efi_update_info_get_hw_inst(st_inf);
	self->status = fu_struct_efi_update_info_get_status(st_inf);
	self->guid = fwupd_guid_to_string(fu_struct_efi_update_info_get_guid(st_inf),
					  FWUPD_GUID_FLAG_MIXED_ENDIAN);
	if (bufsz > FU_STRUCT_EFI_UPDATE_INFO_SIZE) {
		g_autoptr(FuFirmware) dp = NULL;
		g_autoptr(FuEfiDevicePathList) dpbuf = fu_efi_device_path_list_new();
		if (!fu_firmware_parse_full(FU_FIRMWARE(dpbuf),
					    fw,
					    FU_STRUCT_EFI_UPDATE_INFO_SIZE,
					    FWUPD_INSTALL_FLAG_NONE,
					    error)) {
			g_prefix_error(error, "failed to parse dpbuf: ");
			return FALSE;
		}
		dp = fu_firmware_get_image_by_gtype(FU_FIRMWARE(dpbuf),
						    FU_TYPE_EFI_FILE_PATH_DEVICE_PATH,
						    error);
		if (dp == NULL)
			return FALSE;
		self->capsule_fn =
		    fu_efi_file_path_device_path_get_name(FU_EFI_FILE_PATH_DEVICE_PATH(dp), error);
		if (self->capsule_fn == NULL)
			return FALSE;
	}
	return TRUE;
}

const gchar *
fu_uefi_update_info_get_guid(FuUefiUpdateInfo *self)
{
	g_return_val_if_fail(FU_IS_UEFI_UPDATE_INFO(self), NULL);
	return self->guid;
}

const gchar *
fu_uefi_update_info_get_capsule_fn(FuUefiUpdateInfo *self)
{
	g_return_val_if_fail(FU_IS_UEFI_UPDATE_INFO(self), NULL);
	return self->capsule_fn;
}

guint32
fu_uefi_update_info_get_capsule_flags(FuUefiUpdateInfo *self)
{
	g_return_val_if_fail(FU_IS_UEFI_UPDATE_INFO(self), 0);
	return self->capsule_flags;
}

guint64
fu_uefi_update_info_get_hw_inst(FuUefiUpdateInfo *self)
{
	g_return_val_if_fail(FU_IS_UEFI_UPDATE_INFO(self), 0);
	return self->hw_inst;
}

FuUefiUpdateInfoStatus
fu_uefi_update_info_get_status(FuUefiUpdateInfo *self)
{
	g_return_val_if_fail(FU_IS_UEFI_UPDATE_INFO(self), 0);
	return self->status;
}

static void
fu_uefi_update_info_finalize(GObject *object)
{
	FuUefiUpdateInfo *self = FU_UEFI_UPDATE_INFO(object);
	g_free(self->guid);
	g_free(self->capsule_fn);
	G_OBJECT_CLASS(fu_uefi_update_info_parent_class)->finalize(object);
}

static void
fu_uefi_update_info_class_init(FuUefiUpdateInfoClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	klass_firmware->parse = fu_uefi_update_info_parse;
	klass_firmware->export = fu_uefi_update_info_export;
	object_class->finalize = fu_uefi_update_info_finalize;
}

static void
fu_uefi_update_info_init(FuUefiUpdateInfo *self)
{
}

FuUefiUpdateInfo *
fu_uefi_update_info_new(void)
{
	FuUefiUpdateInfo *self;
	self = g_object_new(FU_TYPE_UEFI_UPDATE_INFO, NULL);
	return FU_UEFI_UPDATE_INFO(self);
}
