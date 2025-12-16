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
	guint offset = FU_STRUCT_LEGION_HID_BIN_HEADER_SIZE;
	g_autoptr(FuFirmware) img_left = fu_firmware_new();
	g_autoptr(FuFirmware) img_mcu = fu_firmware_new();
	g_autoptr(FuFirmware) img_right = fu_firmware_new();
	g_autoptr(FuStructLegionHidBinHeader) st_header = NULL;
	g_autoptr(GInputStream) stream_left = NULL;
	g_autoptr(GInputStream) stream_mcu = NULL;
	g_autoptr(GInputStream) stream_right = NULL;

	st_header = fu_struct_legion_hid_bin_header_parse_stream(stream, 0x00, error);
	if (st_header == NULL)
		return FALSE;

	/* MCU */
	stream_mcu =
	    fu_partial_input_stream_new(stream,
					offset,
					fu_struct_legion_hid_bin_header_get_mcu_size(st_header),
					error);
	if (stream_mcu == NULL)
		return FALSE;
	if (!fu_firmware_parse_stream(img_mcu, stream_mcu, 0x00, flags, error))
		return FALSE;
	fu_firmware_set_id(img_mcu, FU_LEGION_HID_FIRMWARE_ID_MCU);
	fu_firmware_set_offset(img_mcu, offset);
	fu_firmware_set_version_format(img_mcu, FWUPD_VERSION_FORMAT_PLAIN);
	fu_firmware_set_version_raw(img_mcu,
				    fu_struct_legion_hid_bin_header_get_mcu_version(st_header));
	if (!fu_firmware_add_image(firmware, img_mcu, error))
		return FALSE;
	offset += fu_struct_legion_hid_bin_header_get_mcu_size(st_header);

	/* left */
	stream_left =
	    fu_partial_input_stream_new(stream,
					offset,
					fu_struct_legion_hid_bin_header_get_left_size(st_header),
					error);
	if (stream_left == NULL)
		return FALSE;
	if (!fu_firmware_parse_stream(img_left, stream_left, 0x00, flags, error))
		return FALSE;
	fu_firmware_set_id(img_left, FU_LEGION_HID_FIRMWARE_ID_LEFT);
	fu_firmware_set_offset(img_left, offset);
	fu_firmware_set_version_format(img_left, FWUPD_VERSION_FORMAT_PLAIN);
	fu_firmware_set_version_raw(img_left,
				    fu_struct_legion_hid_bin_header_get_left_version(st_header));
	if (!fu_firmware_add_image(firmware, img_left, error))
		return FALSE;
	offset += fu_struct_legion_hid_bin_header_get_left_size(st_header);

	/* right */
	stream_right =
	    fu_partial_input_stream_new(stream,
					offset,
					fu_struct_legion_hid_bin_header_get_right_size(st_header),
					error);
	if (stream_right == NULL)
		return FALSE;
	if (!fu_firmware_parse_stream(img_right, stream_right, 0x00, flags, error))
		return FALSE;
	fu_firmware_set_id(img_right, FU_LEGION_HID_FIRMWARE_ID_RIGHT);
	fu_firmware_set_offset(img_right, offset);
	fu_firmware_set_version_format(img_right, FWUPD_VERSION_FORMAT_PLAIN);
	fu_firmware_set_version_raw(img_right,
				    fu_struct_legion_hid_bin_header_get_right_version(st_header));
	if (!fu_firmware_add_image(firmware, img_right, error))
		return FALSE;

	/* success */
	return TRUE;
}

static void
fu_legion_hid_firmware_init(FuLegionHidFirmware *self)
{
	fu_firmware_add_image_gtype(FU_FIRMWARE(self), FU_TYPE_FIRMWARE);
}

static void
fu_legion_hid_firmware_class_init(FuLegionHidFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_legion_hid_firmware_parse;
}
