/*
 * Copyright 2019 GOODIX
 * Copyright 2026 Sunwinon Electronics Co., Ltd.
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-sunwinon-hid-firmware.h"

#define FU_SUNWINON_HID_DFU_SIGN_LEN 856

#define FU_SUNWINON_HID_PATTERN_OFFSET_DEADBEEF 48
#define FU_SUNWINON_HID_PATTERN_OFFSET_SIGN	120

struct _FuSunwinonHidFirmware {
	FuFirmware parent_instance;
	guint32 bin_size;
	guint32 load_addr;
	guint32 full_checksum;
	FuSunwinonFwType fw_type;
};

G_DEFINE_TYPE(FuSunwinonHidFirmware, fu_sunwinon_hid_firmware, FU_TYPE_FIRMWARE)

static void
fu_sunwinon_hid_firmware_export(FuFirmware *firmware,
				FuFirmwareExportFlags flags,
				XbBuilderNode *bn)
{
	FuSunwinonHidFirmware *self = FU_SUNWINON_HID_FIRMWARE(firmware);
	fu_xmlb_builder_insert_kx(bn, "bin_size", self->bin_size);
	fu_xmlb_builder_insert_kx(bn, "load_addr", self->load_addr);
	fu_xmlb_builder_insert_kx(bn, "full_checksum", self->full_checksum);
	fu_xmlb_builder_insert_kv(bn, "fw_type", fu_sunwinon_fw_type_to_string(self->fw_type));
}

guint32
fu_sunwinon_hid_firmware_get_full_checksum(FuSunwinonHidFirmware *self)
{
	g_return_val_if_fail(FU_IS_SUNWINON_HID_FIRMWARE(self), G_MAXUINT32);
	return self->full_checksum;
}

FuSunwinonFwType
fu_sunwinon_hid_firmware_get_fw_type(FuSunwinonHidFirmware *self)
{
	g_return_val_if_fail(FU_IS_SUNWINON_HID_FIRMWARE(self), G_MAXUINT32);
	return self->fw_type;
}

static gboolean
fu_sunwinon_hid_firmware_parse(FuFirmware *firmware,
			       GInputStream *stream,
			       FuFirmwareParseFlags flags,
			       GError **error)
{
	FuSunwinonHidFirmware *self = FU_SUNWINON_HID_FIRMWARE(firmware);
	gsize streamsz = 0;
	gsize tail_size = 0;
	g_autoptr(FuStructSunwinonDfuImageInfo) st = NULL;

	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	if (streamsz < FU_STRUCT_SUNWINON_DFU_IMAGE_INFO_SIZE) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "file is too small");
		return FALSE;
	}
	st = fu_struct_sunwinon_dfu_image_info_parse_stream(
	    stream,
	    streamsz - FU_STRUCT_SUNWINON_DFU_IMAGE_INFO_SIZE,
	    error);
	if (st == NULL)
		return FALSE;

	/* embedded checksum only counts app blob, tailing info is not included --
	 * but the ProgramEnd command requires the full file checksum */
	self->full_checksum = fu_struct_sunwinon_dfu_image_info_get_checksum(st) +
			      fu_sum32(st->buf->data, st->buf->len);
	self->load_addr = fu_struct_sunwinon_dfu_image_info_get_load_addr(st);
	if (self->load_addr % 0x1000 != 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "firmware load address not aligned");
		return FALSE;
	}
	self->bin_size = fu_struct_sunwinon_dfu_image_info_get_bin_size(st);

	/* check fw sign pattern to see if it is signed */
	if (streamsz >= self->bin_size + FU_STRUCT_SUNWINON_DFU_IMAGE_INFO_SIZE +
			    FU_SUNWINON_HID_DFU_SIGN_LEN) {
		guint32 fw_pattern_deadbeef = 0;
		guint32 fw_pattern_sign = 0;

		if (!fu_input_stream_read_u32(stream,
					      self->bin_size +
						  FU_SUNWINON_HID_PATTERN_OFFSET_DEADBEEF,
					      &fw_pattern_deadbeef,
					      G_LITTLE_ENDIAN,
					      error))
			return FALSE;
		if (!fu_input_stream_read_u32(stream,
					      self->bin_size + FU_SUNWINON_HID_PATTERN_OFFSET_SIGN,
					      &fw_pattern_sign,
					      G_LITTLE_ENDIAN,
					      error))
			return FALSE;
		if ((fw_pattern_deadbeef == 0xDEADBEEF) &&
		    (fw_pattern_sign == 0x4E474953)) { /* "SIGN" */
			self->fw_type = FU_SUNWINON_FW_TYPE_SIGNED;
			tail_size += FU_SUNWINON_HID_DFU_SIGN_LEN;
			g_debug("signed firmware (sign pattern found)");
		}
	}

	/* check if the fw is correctly packed */
	if (streamsz != self->bin_size + FU_STRUCT_SUNWINON_DFU_IMAGE_INFO_SIZE + tail_size) {
		g_set_error(
		    error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_INVALID_FILE,
		    "firmware size mismatch, got 0x%x but expected %x",
		    (guint)streamsz,
		    (guint)(self->bin_size + FU_STRUCT_SUNWINON_DFU_IMAGE_INFO_SIZE + tail_size));
		return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_sunwinon_hid_firmware_init(FuSunwinonHidFirmware *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_STORED_SIZE);
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_CHECKSUM);
}

static void
fu_sunwinon_hid_firmware_class_init(FuSunwinonHidFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_sunwinon_hid_firmware_parse;
	firmware_class->export = fu_sunwinon_hid_firmware_export;
}
