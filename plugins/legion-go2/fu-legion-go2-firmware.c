/*
 * Copyright 2025 hya1711 <591770796@qq.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-legion-go2-firmware.h"
#include "fu-legion-go2-struct.h"


struct _FuLegionGo2Firmware {
	FuFirmware parent_instance;
};

G_DEFINE_TYPE(FuLegionGo2Firmware, fu_legion_go2_firmware, FU_TYPE_FIRMWARE)

static gboolean
fu_legion_go2_firmware_parse(FuFirmware *firmware,
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
	g_autoptr(FuStructLegionGo2BinHeader) header = NULL;

	header = fu_struct_legion_go2_bin_header_parse_stream(stream, 0x00, error);
	if (header == NULL)
	{
		return FALSE;
	}

	// mcu
	guint offset = FU_STRUCT_LEGION_GO2_BIN_HEADER_SIZE;
	stream_mcu = fu_partial_input_stream_new(stream,
						 offset,
						 fu_struct_legion_go2_bin_header_get_mcu_size(header),
						 error);
	if (stream_mcu == NULL)
	{
		return FALSE;
	}
	if (!fu_firmware_parse_stream(img_mcu, stream_mcu, 0x00, flags, error))
	{
		return FALSE;
	}
	fu_firmware_set_id(img_mcu, "DeviceIDRx");
	fu_firmware_set_version_raw(img_mcu, fu_struct_legion_go2_bin_header_get_mcu_version(header));
	if (!fu_firmware_add_image(firmware, img_mcu, error))
	{
		return FALSE;
	}

	// left
	offset += fu_struct_legion_go2_bin_header_get_mcu_size(header);
	stream_left = fu_partial_input_stream_new(stream,
						  offset,
						  fu_struct_legion_go2_bin_header_get_left_size(header),
						  error);
	if (stream_left == NULL)
	{
		return FALSE;
	}
	if (!fu_firmware_parse_stream(img_left, stream_left, 0x00, flags, error))
	{
		return FALSE;
	}
	fu_firmware_set_id(img_left, "DeviceIDGamepadL");
	fu_firmware_set_version_raw(img_left, fu_struct_legion_go2_bin_header_get_left_version(header));
	if (!fu_firmware_add_image(firmware, img_left, error))
	{
		return FALSE;
	}

	// right
	offset += fu_struct_legion_go2_bin_header_get_left_size(header);
	stream_right = fu_partial_input_stream_new(stream,
						  offset,
						  fu_struct_legion_go2_bin_header_get_right_size(header),
						  error);
	if (stream_right == NULL)
	{
		return FALSE;
	}
	if (!fu_firmware_parse_stream(img_right, stream_right, 0x00, flags, error))
	{
		return FALSE;
	}
	fu_firmware_set_id(img_right, "DeviceIDGamepadR");
	fu_firmware_set_version_raw(img_right, fu_struct_legion_go2_bin_header_get_right_version(header));
	if (!fu_firmware_add_image(firmware, img_right, error))
	{
		return FALSE;
	}

	return TRUE;
}

static void
fu_legion_go2_firmware_init(FuLegionGo2Firmware *self)
{
	
}

static void
fu_legion_go2_firmware_class_init(FuLegionGo2FirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_legion_go2_firmware_parse;
}

FuFirmware *
fu_legion_go2_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_LEGION_GO2_FIRMWARE, NULL));
}
