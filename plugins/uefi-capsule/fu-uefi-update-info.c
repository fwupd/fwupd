/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
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

void
fu_uefi_update_info_set_guid(FuUefiUpdateInfo *self, const gchar *guid)
{
	g_free(self->guid);
	self->guid = g_strdup(guid);
}

void
fu_uefi_update_info_set_capsule_fn(FuUefiUpdateInfo *self, const gchar *capsule_fn)
{
	g_free(self->capsule_fn);
	self->capsule_fn = g_strdup(capsule_fn);
}

void
fu_uefi_update_info_set_capsule_flags(FuUefiUpdateInfo *self, guint32 capsule_flags)
{
	self->capsule_flags = capsule_flags;
}

void
fu_uefi_update_info_set_hw_inst(FuUefiUpdateInfo *self, guint64 hw_inst)
{
	self->hw_inst = hw_inst;
}

void
fu_uefi_update_info_set_status(FuUefiUpdateInfo *self, FuUefiUpdateInfoStatus status)
{
	self->status = status;
}

static gboolean
fu_uefi_update_info_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuUefiUpdateInfo *self = FU_UEFI_UPDATE_INFO(firmware);
	const gchar *tmp;

	tmp = xb_node_query_text(n, "guid", NULL);
	if (tmp != NULL)
		fu_uefi_update_info_set_guid(self, tmp);
	tmp = xb_node_query_text(n, "capsule_fn", NULL);
	if (tmp != NULL)
		fu_uefi_update_info_set_capsule_fn(self, tmp);
	tmp = xb_node_query_text(n, "capsule_flags", NULL);
	if (tmp != NULL) {
		guint64 tmp64 = 0;
		if (!fu_strtoull(tmp, &tmp64, 0, G_MAXUINT32, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		fu_uefi_update_info_set_capsule_flags(self, tmp64);
	}
	tmp = xb_node_query_text(n, "hw_inst", NULL);
	if (tmp != NULL) {
		guint64 tmp64 = 0;
		if (!fu_strtoull(tmp, &tmp64, 0, G_MAXUINT64, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		fu_uefi_update_info_set_hw_inst(self, tmp64);
	}
	tmp = xb_node_query_text(n, "status", NULL);
	if (tmp != NULL) {
		self->status = fu_uefi_update_info_status_from_string(tmp);
		if (self->status == FU_UEFI_UPDATE_INFO_STATUS_UNKNOWN) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "status %s not supported",
				    tmp);
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static GByteArray *
fu_uefi_update_info_write(FuFirmware *firmware, GError **error)
{
	FuUefiUpdateInfo *self = FU_UEFI_UPDATE_INFO(firmware);
	fwupd_guid_t guid = {0};
	g_autoptr(FuStructEfiUpdateInfo) st = fu_struct_efi_update_info_new();

	if (!fwupd_guid_from_string(self->guid, &guid, FWUPD_GUID_FLAG_MIXED_ENDIAN, error))
		return NULL;
	fu_struct_efi_update_info_set_guid(st, &guid);
	fu_struct_efi_update_info_set_flags(st, self->capsule_flags);
	fu_struct_efi_update_info_set_hw_inst(st, self->hw_inst);
	fu_struct_efi_update_info_set_status(st, self->status);
	if (self->capsule_fn != NULL) {
		g_autoptr(GBytes) dpbuf = NULL;
		g_autoptr(FuEfiDevicePathList) dp_list = fu_efi_device_path_list_new();
		g_autoptr(FuEfiFilePathDevicePath) dp_fp = fu_efi_file_path_device_path_new();
		if (!fu_efi_file_path_device_path_set_name(dp_fp, self->capsule_fn, error))
			return NULL;
		fu_firmware_add_image(FU_FIRMWARE(dp_list), FU_FIRMWARE(dp_fp));
		dpbuf = fu_firmware_write(FU_FIRMWARE(dp_list), error);
		if (dpbuf == NULL)
			return NULL;
		fu_byte_array_append_bytes(st, dpbuf);
	}

	/* success */
	return g_steal_pointer(&st);
}

static gboolean
fu_uefi_update_info_parse(FuFirmware *firmware,
			  GInputStream *stream,
			  FwupdInstallFlags flags,
			  GError **error)
{
	FuUefiUpdateInfo *self = FU_UEFI_UPDATE_INFO(firmware);
	gsize streamsz = 0;
	g_autoptr(GByteArray) st_inf = NULL;

	g_return_val_if_fail((self), FALSE);

	st_inf = fu_struct_efi_update_info_parse_stream(stream, 0x0, error);
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
	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	if (streamsz > FU_STRUCT_EFI_UPDATE_INFO_SIZE) {
		g_autoptr(FuFirmware) dp = NULL;
		g_autoptr(FuEfiDevicePathList) dpbuf = fu_efi_device_path_list_new();
		if (!fu_firmware_parse_stream(FU_FIRMWARE(dpbuf),
					      stream,
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
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	firmware_class->parse = fu_uefi_update_info_parse;
	firmware_class->export = fu_uefi_update_info_export;
	firmware_class->build = fu_uefi_update_info_build;
	firmware_class->write = fu_uefi_update_info_write;
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
