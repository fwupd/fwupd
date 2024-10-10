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
};

G_DEFINE_TYPE(FuLegionHid2Firmware, fu_legion_hid2_firmware, FU_TYPE_FIRMWARE)

static void
fu_legion_hid2_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuLegionHid2Firmware *self = FU_LEGION_HID2_FIRMWARE(firmware);
	g_autofree gchar *version =
	    fu_version_from_uint32(self->version, FWUPD_VERSION_FORMAT_QUAD);

	fu_xmlb_builder_insert_kv(bn, "version", version);
}

guint32
fu_legion_hid2_firmware_get_version(FuFirmware *firmware)
{
	FuLegionHid2Firmware *self = FU_LEGION_HID2_FIRMWARE(firmware);
	return self->version;
}

static gboolean
fu_legion_hid2_firmware_parse(FuFirmware *firmware,
			      GInputStream *stream,
			      FwupdInstallFlags flags,
			      GError **error)
{
	FuLegionHid2Firmware *self = FU_LEGION_HID2_FIRMWARE(firmware);
	g_autoptr(FuFirmware) img_payload = fu_firmware_new();
	g_autoptr(FuFirmware) img_sig = fu_firmware_new();
	g_autoptr(GInputStream) stream_payload = NULL;
	g_autoptr(GInputStream) stream_sig = NULL;
	g_autoptr(GByteArray) header = NULL;
	g_autoptr(GByteArray) version = NULL;

	header = fu_struct_legion_hid2_header_parse_stream(stream, 0x0, error);
	if (header == NULL)
		return FALSE;

	stream_sig = fu_partial_input_stream_new(stream,
						 fu_struct_legion_hid2_header_get_sig_add(header),
						 fu_struct_legion_hid2_header_get_sig_len(header),
						 error);
	if (stream_sig == NULL)
		return FALSE;
	if (!fu_firmware_parse_stream(img_sig, stream_sig, 0x0, flags, error))
		return FALSE;
	fu_firmware_set_id(img_sig, FU_FIRMWARE_ID_SIGNATURE);
	fu_firmware_add_image(firmware, img_sig);

	stream_payload =
	    fu_partial_input_stream_new(stream,
					fu_struct_legion_hid2_header_get_data_add(header),
					fu_struct_legion_hid2_header_get_data_len(header),
					error);
	if (stream_payload == NULL)
		return FALSE;
	if (!fu_firmware_parse_stream(img_payload, stream_payload, 0x0, flags, error))
		return FALSE;
	fu_firmware_set_id(img_payload, FU_FIRMWARE_ID_PAYLOAD);
	fu_firmware_add_image(firmware, img_payload);

	version = fu_struct_legion_hid2_version_parse_stream(stream, VERSION_OFFSET, error);
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
