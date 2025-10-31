/*
 * Copyright 2025 hya1711 <591770796@qq.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-legion-hid-firmware.h"
#include "fu-legion-hid-struct.h"

struct _FuLegionHidFirmware {
	FuFirmware parent_instance;
};

G_DEFINE_TYPE(FuLegionHidFirmware, fu_legion_hid_firmware, FU_TYPE_FIRMWARE)

static gboolean
fu_legion_hid_firmware_parse(FuFirmware *firmware,
			     GInputStream *stream,
			     FuFirmwareParseFlags flags,
			     GError **error)
{
	g_autoptr(FuFirmware) img_mcu = fu_firmware_new();
	g_autoptr(FuFirmware) img_left = fu_firmware_new();
	g_autoptr(FuFirmware) img_right = fu_firmware_new();
	g_autoptr(GInputStream) stream_mcu = NULL;
	g_autoptr(GInputStream) stream_left = NULL;
	g_autoptr(GInputStream) stream_right = NULL;
	g_autoptr(FuStructLegionHidBinHeader) st_header = NULL;
	guint offset = FU_STRUCT_LEGION_HID_BIN_HEADER_SIZE;

	st_header = fu_struct_legion_hid_bin_header_parse_stream(stream, 0x00, error);
	if (st_header == NULL)
		return FALSE;

	stream_mcu =
	    fu_partial_input_stream_new(stream,
					offset,
					fu_struct_legion_hid_bin_header_get_mcu_size(st_header),
					error);
	if (stream_mcu == NULL)
		return FALSE;
	if (!fu_firmware_parse_stream(img_mcu, stream_mcu, 0x00, flags, error))
		return FALSE;
	fu_firmware_set_id(img_mcu, "DeviceIDRx");
	fu_firmware_set_version_raw(img_mcu,
				    fu_struct_legion_hid_bin_header_get_mcu_version(st_header));
	if (!fu_firmware_add_image(firmware, img_mcu, error))
		return FALSE;

	offset += fu_struct_legion_hid_bin_header_get_mcu_size(st_header);
	stream_left =
	    fu_partial_input_stream_new(stream,
					offset,
					fu_struct_legion_hid_bin_header_get_left_size(st_header),
					error);
	if (stream_left == NULL)
		return FALSE;
	if (!fu_firmware_parse_stream(img_left, stream_left, 0x00, flags, error))
		return FALSE;
	fu_firmware_set_id(img_left, "DeviceIDGamepadL");
	fu_firmware_set_version_raw(img_left,
				    fu_struct_legion_hid_bin_header_get_left_version(st_header));
	if (!fu_firmware_add_image(firmware, img_left, error))
		return FALSE;

	offset += fu_struct_legion_hid_bin_header_get_left_size(st_header);
	stream_right =
	    fu_partial_input_stream_new(stream,
					offset,
					fu_struct_legion_hid_bin_header_get_right_size(st_header),
					error);
	if (stream_right == NULL)
		return FALSE;
	if (!fu_firmware_parse_stream(img_right, stream_right, 0x00, flags, error))
		return FALSE;
	fu_firmware_set_id(img_right, "DeviceIDGamepadR");
	fu_firmware_set_version_raw(img_right,
				    fu_struct_legion_hid_bin_header_get_right_version(st_header));
	if (!fu_firmware_add_image(firmware, img_right, error))
		return FALSE;

	return TRUE;
}

static void
fu_legion_hid_firmware_init(FuLegionHidFirmware *self)
{
}

static void
fu_legion_hid_firmware_class_init(FuLegionHidFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_legion_hid_firmware_parse;
}
