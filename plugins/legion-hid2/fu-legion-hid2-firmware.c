/*
 * Copyright 2024 Mario Limonciello <superm1@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-legion-hid2-firmware.h"
#include "fu-legion-hid2-struct.h"

#define VERSION_OFFSET 0x1e0

struct _FuLegionHid2Firmware {
	FuFirmware parent_instance;
	guint32 version;
	guint32 sig_size;
	guint32 data_size;
	guint32 sig_offset;
	guint32 data_offset;
};

G_DEFINE_TYPE(FuLegionHid2Firmware, fu_legion_hid2_firmware, FU_TYPE_FIRMWARE)

static void
fu_legion_hid2_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuLegionHid2Firmware *self = FU_LEGION_HID2_FIRMWARE(firmware);
	g_autofree gchar *version =
	    fu_version_from_uint32(self->version, FWUPD_VERSION_FORMAT_QUAD);

	fu_xmlb_builder_insert_kv(bn, "version", version);
	fu_xmlb_builder_insert_kx(bn, "sig_offset", self->sig_offset);
	fu_xmlb_builder_insert_kx(bn, "sig_size", self->sig_size);
	fu_xmlb_builder_insert_kx(bn, "data_offset", self->data_offset);
	fu_xmlb_builder_insert_kx(bn, "data_size", self->data_size);
}

guint32
fu_legion_hid2_firmware_get_sig_offset(FuFirmware *firmware)
{
	FuLegionHid2Firmware *self = FU_LEGION_HID2_FIRMWARE(firmware);
	return self->sig_offset;
}

gssize
fu_legion_hid2_firmware_get_sig_size(FuFirmware *firmware)
{
	FuLegionHid2Firmware *self = FU_LEGION_HID2_FIRMWARE(firmware);
	return self->sig_size;
}

guint32
fu_legion_hid2_firmware_get_data_offset(FuFirmware *firmware)
{
	FuLegionHid2Firmware *self = FU_LEGION_HID2_FIRMWARE(firmware);
	return self->data_offset;
}

gssize
fu_legion_hid2_firmware_get_data_size(FuFirmware *firmware)
{
	FuLegionHid2Firmware *self = FU_LEGION_HID2_FIRMWARE(firmware);
	return self->data_size;
}

guint32
fu_legion_hid2_firmware_get_version(FuFirmware *firmware)
{
	FuLegionHid2Firmware *self = FU_LEGION_HID2_FIRMWARE(firmware);
	return self->version;
}

static gboolean
fu_legion_hid2_firmware_parse(FuFirmware *firmware,
			      GBytes *fw,
			      gsize offset,
			      FwupdInstallFlags flags,
			      GError **error)
{
	FuLegionHid2Firmware *self = FU_LEGION_HID2_FIRMWARE(firmware);
	gsize bufsz;
	g_autoptr(GByteArray) header = NULL;
	g_autoptr(GByteArray) version = NULL;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);

	header = fu_struct_legion_hid2_header_parse(buf, bufsz, 0x0, error);
	if (header == NULL)
		return FALSE;
	self->sig_offset = fu_struct_legion_hid2_header_get_sig_add(header);
	self->sig_size = fu_struct_legion_hid2_header_get_sig_len(header);
	self->data_offset = fu_struct_legion_hid2_header_get_data_add(header);
	self->data_size = fu_struct_legion_hid2_header_get_data_len(header);

	buf = g_bytes_get_data(fw, &bufsz);
	version = fu_struct_legion_hid2_version_parse(buf, bufsz, VERSION_OFFSET, error);
	if (version == NULL)
		return FALSE;
	self->version = fu_struct_legion_hid2_version_get_version(version);

	return TRUE;
}

static void
fu_legion_hid2_firmware_init(FuLegionHid2Firmware *self)
{
}

static void
fu_legion_hid2_firmware_class_init(FuLegionHid2FirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_legion_hid2_firmware_parse;
	firmware_class->export = fu_legion_hid2_firmware_export;
}

FuFirmware *
fu_legion_hid2_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_LEGION_HID2_FIRMWARE, NULL));
}
