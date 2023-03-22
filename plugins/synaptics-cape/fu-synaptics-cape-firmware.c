/*
 * Copyright (C) 2021 Synaptics Incorporated <simon.ho@synaptics.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include <string.h>

#include "fu-synaptics-cape-firmware.h"
#include "fu-synaptics-cape-struct.h"

struct _FuSynapticsCapeFirmware {
	FuFirmware parent_instance;
	guint16 vid;
	guint16 pid;
};

G_DEFINE_TYPE(FuSynapticsCapeFirmware, fu_synaptics_cape_firmware, FU_TYPE_FIRMWARE)

guint16
fu_synaptics_cape_firmware_get_vid(FuSynapticsCapeFirmware *self)
{
	g_return_val_if_fail(FU_IS_SYNAPTICS_CAPE_FIRMWARE(self), 0);
	return self->vid;
}

guint16
fu_synaptics_cape_firmware_get_pid(FuSynapticsCapeFirmware *self)
{
	g_return_val_if_fail(FU_IS_SYNAPTICS_CAPE_FIRMWARE(self), 0);
	return self->pid;
}

static void
fu_synaptics_cape_firmware_export(FuFirmware *firmware,
				  FuFirmwareExportFlags flags,
				  XbBuilderNode *bn)
{
	FuSynapticsCapeFirmware *self = FU_SYNAPTICS_CAPE_FIRMWARE(firmware);
	fu_xmlb_builder_insert_kx(bn, "vid", self->vid);
	fu_xmlb_builder_insert_kx(bn, "pid", self->pid);
}

static gboolean
fu_synaptics_cape_firmware_parse(FuFirmware *firmware,
				 GBytes *fw,
				 gsize offset,
				 FwupdInstallFlags flags,
				 GError **error)
{
	FuSynapticsCapeFirmware *self = FU_SYNAPTICS_CAPE_FIRMWARE(firmware);
	gsize bufsz = 0;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);
	g_autofree gchar *version_str = NULL;
	g_autoptr(FuFirmware) img_hdr = fu_firmware_new();
	g_autoptr(GByteArray) st = NULL;
	g_autoptr(GBytes) fw_body = NULL;
	g_autoptr(GBytes) fw_hdr = NULL;

	/* sanity check */
	if ((guint32)bufsz % 4 != 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "data not aligned to 32 bits");
		return FALSE;
	}

	/* unpack */
	st = fu_struct_synaptics_cape_file_hdr_parse(buf, bufsz, offset, error);
	if (st == NULL)
		return FALSE;
	self->vid = fu_struct_synaptics_cape_file_hdr_get_vid(st);
	self->pid = fu_struct_synaptics_cape_file_hdr_get_pid(st);
	version_str = g_strdup_printf("%u.%u.%u.%u",
				      fu_struct_synaptics_cape_file_hdr_get_ver_z(st),
				      fu_struct_synaptics_cape_file_hdr_get_ver_y(st),
				      fu_struct_synaptics_cape_file_hdr_get_ver_x(st),
				      fu_struct_synaptics_cape_file_hdr_get_ver_w(st));
	fu_firmware_set_version(FU_FIRMWARE(self), version_str);

	/* top-most part of header */
	fw_hdr = fu_bytes_new_offset(fw, 0, FU_STRUCT_SYNAPTICS_CAPE_FILE_HDR_OFFSET_VER_W, error);
	if (fw_hdr == NULL)
		return FALSE;
	fu_firmware_set_id(img_hdr, FU_FIRMWARE_ID_HEADER);
	fu_firmware_set_bytes(img_hdr, fw_hdr);
	fu_firmware_add_image(firmware, img_hdr);

	/* body */
	fw_body = fu_bytes_new_offset(fw, st->len, bufsz - st->len, error);
	if (fw_body == NULL)
		return FALSE;
	fu_firmware_set_id(firmware, FU_FIRMWARE_ID_PAYLOAD);
	fu_firmware_set_bytes(firmware, fw_body);
	return TRUE;
}

static GBytes *
fu_synaptics_cape_firmware_write(FuFirmware *firmware, GError **error)
{
	FuSynapticsCapeFirmware *self = FU_SYNAPTICS_CAPE_FIRMWARE(firmware);
	guint64 ver = fu_firmware_get_version_raw(firmware);
	g_autoptr(GByteArray) buf = fu_struct_synaptics_cape_file_hdr_new();
	g_autoptr(GBytes) payload = NULL;

	/* pack */
	fu_struct_synaptics_cape_file_hdr_set_vid(buf, self->vid);
	fu_struct_synaptics_cape_file_hdr_set_pid(buf, self->pid);
	fu_struct_synaptics_cape_file_hdr_set_crc(buf, 0xFFFF);
	fu_struct_synaptics_cape_file_hdr_set_ver_w(buf, ver >> 0);
	fu_struct_synaptics_cape_file_hdr_set_ver_x(buf, ver >> 16);
	fu_struct_synaptics_cape_file_hdr_set_ver_y(buf, ver >> 32);
	fu_struct_synaptics_cape_file_hdr_set_ver_z(buf, ver >> 48);

	/* payload */
	payload = fu_firmware_get_bytes_with_patches(firmware, error);
	if (payload == NULL)
		return NULL;
	fu_byte_array_append_bytes(buf, payload);
	fu_byte_array_align_up(buf, FU_FIRMWARE_ALIGNMENT_32, 0xFF);

	return g_byte_array_free_to_bytes(g_steal_pointer(&buf));
}

static gboolean
fu_synaptics_cape_firmware_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuSynapticsCapeFirmware *self = FU_SYNAPTICS_CAPE_FIRMWARE(firmware);
	guint64 tmp;

	/* optional properties */
	tmp = xb_node_query_text_as_uint(n, "vid", NULL);
	if (tmp != G_MAXUINT64 && tmp <= G_MAXUINT16)
		self->vid = tmp;
	tmp = xb_node_query_text_as_uint(n, "pid", NULL);
	if (tmp != G_MAXUINT64 && tmp <= G_MAXUINT16)
		self->pid = tmp;

	/* success */
	return TRUE;
}

static void
fu_synaptics_cape_firmware_init(FuSynapticsCapeFirmware *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_VID_PID);
}

static void
fu_synaptics_cape_firmware_class_init(FuSynapticsCapeFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->parse = fu_synaptics_cape_firmware_parse;
	klass_firmware->export = fu_synaptics_cape_firmware_export;
	klass_firmware->write = fu_synaptics_cape_firmware_write;
	klass_firmware->build = fu_synaptics_cape_firmware_build;
}

FuFirmware *
fu_synaptics_cape_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_SYNAPTICS_CAPE_FIRMWARE, NULL));
}
