/*
 * Copyright 2026 Elan Microelectronics Corporation <paul.liang@emc.com.tw>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-elan-ts-firmware.h"
#include "fu-elan-ts-struct.h"

struct _FuElanTsFirmware {
	FuFirmware parent_instance;
	FuElanTsFwType fw_type;
	FuElanTsDebugSetting debug_setting;
	guint16 remark_id;
};

G_DEFINE_TYPE(FuElanTsFirmware, fu_elan_ts_firmware, FU_TYPE_FIRMWARE)

/* parameter addresses in firmware binary (last page) */
#define FU_ELAN_TS_PARAM_ADDR_LAST_PAGE	     0xFFC0
#define FU_ELAN_TS_PARAM_ADDR_GEN5_LAST_PAGE 0x7FC0
#define FU_ELAN_TS_PARAM_ADDR_REMARK_ID	     0xFFFF

FuElanTsFwType
fu_elan_ts_firmware_get_fw_type(FuElanTsFirmware *self)
{
	return self->fw_type;
}

FuElanTsDebugSetting
fu_elan_ts_firmware_get_debug_setting(FuElanTsFirmware *self)
{
	return self->debug_setting;
}

guint16
fu_elan_ts_firmware_get_remark_id(FuElanTsFirmware *self)
{
	return self->remark_id;
}

static gboolean
fu_elan_ts_firmware_parse_remark_id(FuElanTsFirmware *self, GInputStream *stream, GError **error)
{
	gsize last_page_offset;
	gsize offset_in_page;
	gsize page_count;
	gsize streamsz = 0;
	guint16 base_addr;
	guint16 last_page_addr = 0;

	/* last page offset */
	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	page_count = streamsz / FU_ELAN_TS_FIRMWARE_PAGE_SIZE;
	last_page_offset = (page_count - 1) * FU_ELAN_TS_FIRMWARE_PAGE_SIZE;

	/* last page address (the first word of the last page) */
	if (!fu_input_stream_read_u16(stream,
				      last_page_offset,
				      &last_page_addr,
				      G_LITTLE_ENDIAN,
				      error)) {
		g_prefix_error_literal(error, "failed to read last page address: ");
		return FALSE;
	}

	if (last_page_addr != FU_ELAN_TS_PARAM_ADDR_LAST_PAGE &&
	    last_page_addr != FU_ELAN_TS_PARAM_ADDR_GEN5_LAST_PAGE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "invalid last page address: 0x%04x",
			    last_page_addr);
		return FALSE;
	}

	/* read remark id */
	base_addr = last_page_addr;
	if (last_page_addr == FU_ELAN_TS_PARAM_ADDR_GEN5_LAST_PAGE)
		base_addr = FU_ELAN_TS_PARAM_ADDR_LAST_PAGE;
	offset_in_page = (1 + (FU_ELAN_TS_PARAM_ADDR_REMARK_ID - base_addr)) * 2;
	if (!fu_input_stream_read_u16(stream,
				      last_page_offset + offset_in_page,
				      &self->remark_id,
				      G_LITTLE_ENDIAN,
				      error)) {
		g_prefix_error_literal(error, "failed to read remark ID: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_elan_ts_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuElanTsFirmware *self = FU_ELAN_TS_FIRMWARE(firmware);
	fu_xmlb_builder_insert_kx(bn, "fw_type", self->fw_type);
	fu_xmlb_builder_insert_kx(bn, "debug_setting", self->debug_setting);
	fu_xmlb_builder_insert_kx(bn, "remark_id", self->remark_id);
}

static gboolean
fu_elan_ts_firmware_validate(FuFirmware *firmware,
			     GInputStream *stream,
			     gsize offset,
			     GError **error)
{
	return fu_struct_elan_ts_fw_bin_header_lvfs_type1_validate_stream(stream, offset, error);
}

static gboolean
fu_elan_ts_firmware_parse(FuFirmware *firmware,
			  GInputStream *stream,
			  FuFirmwareParseFlags flags,
			  GError **error)
{
	FuElanTsFirmware *self = FU_ELAN_TS_FIRMWARE(firmware);
	const guint8 *checksum_buf;
	gsize checksum_bufsz = 0;
	guint32 bin_size;
	g_autofree gchar *checksum_actual_str = NULL;
	g_autofree gchar *checksum_expected_str = NULL;
	g_autoptr(FuStructElanTsFwBinHeaderLvfsType1) st_header = NULL;
	g_autoptr(GByteArray) checksum_array = g_byte_array_new();
	g_autoptr(FuFirmware) img_payload = fu_firmware_new();
	g_autoptr(GInputStream) stream_payload = NULL;

	/* parse header */
	st_header = fu_struct_elan_ts_fw_bin_header_lvfs_type1_parse_stream(stream, 0x0, error);
	if (st_header == NULL) {
		g_prefix_error_literal(error, "failed to get FW BIN Header: ");
		return FALSE;
	}
	self->fw_type = fu_struct_elan_ts_fw_bin_header_lvfs_type1_get_fw_type(st_header);
	self->debug_setting =
	    (FuElanTsDebugSetting)fu_struct_elan_ts_fw_bin_header_lvfs_type1_get_debug(st_header);
	bin_size = fu_struct_elan_ts_fw_bin_header_lvfs_type1_get_bin_size(st_header);

	/* reject non page-aligned binaries */
	if (bin_size == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "firmware binary zero size");
		return FALSE;
	}
	if (bin_size % FU_ELAN_TS_FIRMWARE_PAGE_SIZE != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "firmware binary size %u is not a multiple of %u",
			    bin_size,
			    (guint)FU_ELAN_TS_FIRMWARE_PAGE_SIZE);
		return FALSE;
	}

	/* read the raw FW BIN payload from the stream */
	stream_payload =
	    fu_partial_input_stream_new(stream,
					FU_STRUCT_ELAN_TS_FW_BIN_HEADER_LVFS_TYPE1_SIZE,
					bin_size,
					error);
	if (stream_payload == NULL)
		return FALSE;
	fu_firmware_set_id(img_payload, FU_FIRMWARE_ID_PAYLOAD);
	if (!fu_firmware_set_stream(img_payload, stream_payload, error))
		return FALSE;
	if (!fu_firmware_add_image(firmware, img_payload, error))
		return FALSE;

	/* verify integrity */
	checksum_buf =
	    fu_struct_elan_ts_fw_bin_header_lvfs_type1_get_security_code(st_header,
									 &checksum_bufsz);
	g_byte_array_append(checksum_array, checksum_buf, checksum_bufsz);
	checksum_expected_str = fu_byte_array_to_string(checksum_array);
	checksum_actual_str =
	    fu_input_stream_compute_checksum(stream_payload, G_CHECKSUM_SHA256, error);
	if (checksum_actual_str == NULL)
		return FALSE;
	if (g_strcmp0(checksum_expected_str, checksum_actual_str) != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "checksum failure, expected %s and got %s",
			    checksum_expected_str,
			    checksum_actual_str);
		return FALSE;
	}

	/* parse the remark id */
	if (!fu_elan_ts_firmware_parse_remark_id(self, stream_payload, error)) {
		g_prefix_error_literal(error, "failed to parse remark id: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_elan_ts_firmware_init(FuElanTsFirmware *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_CHECKSUM);
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_STORED_SIZE);
	fu_firmware_set_version_format(FU_FIRMWARE(self), FWUPD_VERSION_FORMAT_HEX);
	fu_firmware_set_size_max(FU_FIRMWARE(self), 4 * FU_MB);
	fu_firmware_add_image_gtype(FU_FIRMWARE(self), FU_TYPE_FIRMWARE);
}

static void
fu_elan_ts_firmware_class_init(FuElanTsFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->validate = fu_elan_ts_firmware_validate;
	firmware_class->parse = fu_elan_ts_firmware_parse;
	firmware_class->export = fu_elan_ts_firmware_export;
}
