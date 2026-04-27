/*
 * Copyright 2026 Elan Microelectronics Corporation <paul.liang@emc.com.tw>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-elan-ts-firmware.h"
#include "fu-elan-ts-common.h"
#include "fu-elan-ts-struct.h"

struct _FuElanTsFirmware {
	FuFirmware              parent_instance;
	FuElanTsFwType          fw_type;
	FuElanTsDebugSetting    debug_setting;
	guint8                  security_code[32];
	guint32                 bin_size;
	guint16                 remark_id;
};

G_DEFINE_TYPE(FuElanTsFirmware, fu_elan_ts_firmware, FU_TYPE_FIRMWARE)

/**
 * elan_ts_firmware_get_fw_type:
 * @self: a #FuElanTsFirmware
 *
 * Gets the firmware type specified in the binary header.
 *
 * Returns: a #FuElanTsFwType, e.g. %FU_ELAN_TS_FW_TYPE_EKT
 **/
FuElanTsFwType
elan_ts_firmware_get_fw_type(FuElanTsFirmware *self)
{
	g_return_val_if_fail(FU_IS_ELAN_TS_FIRMWARE(self), FU_ELAN_TS_FW_TYPE_UNKNOWN);
	return self->fw_type;
}

/**
 * elan_ts_firmware_get_debug_setting:
 * @self: a #FuElanTsFirmware
 *
 * Gets the debug setting bitmask from the firmware header.
 * These settings control logging behavior and update policy overrides.
 *
 * Returns: a #FuElanTsDebugSetting bitmask
 **/
FuElanTsDebugSetting
elan_ts_firmware_get_debug_setting(FuElanTsFirmware *self)
{
	g_return_val_if_fail(FU_IS_ELAN_TS_FIRMWARE(self), FU_ELAN_TS_DEBUG_SETTING_NONE);
	return self->debug_setting;
}

/**
 * elan_ts_firmware_get_bin_size:
 * @self: a #FuElanTsFirmware
 *
 * Returns: the size of fw_bin in bytes
 **/
guint32
elan_ts_firmware_get_bin_size(FuElanTsFirmware *self)
{
	g_return_val_if_fail(FU_IS_ELAN_TS_FIRMWARE(self), 0);
	return self->bin_size;
}

/**
 * elan_ts_firmware_get_page_count:
 * @self: a #FuElanTsFirmware
 *
 * Returns: the number of pages
 **/
guint
elan_ts_firmware_get_page_count(FuElanTsFirmware *self)
{
	g_return_val_if_fail(FU_IS_ELAN_TS_FIRMWARE(self), 0);

	/* 
	 * Each page in the binary is 132 bytes (2 Addr + 128 Data + 2 Checksum).
	 * Uses DIV_ROUND_UP logic to match (size / 132) + (size % 132 != 0).
	 */
	return (guint)((self->bin_size + ELAN_TS_FW_PAGE_SIZE - 1) / ELAN_TS_FW_PAGE_SIZE);
}

/**
 * elan_ts_firmware_get_page:
 * @self: a #FuElanTsFirmware
 * @base_page_index: the index of the first page to retrieve
 * @page_count: number of pages to retrieve
 * @p_page_buf: (out): destination buffer to store the formatted pages
 * @page_buf_size: total size of the destination buffer
 * @error: (nullable): a #GError, or %NULL
 *
 * Extracts pre-formatted 132-byte pages from the firmware binary payload.
 *
 * Returns: %TRUE for success, %FALSE for failure.
 **/
gboolean
elan_ts_firmware_get_page(FuElanTsFirmware *self,
                         guint base_page_index,
                         guint page_count,
                         guint8 *p_page_buf,
                         gsize page_buf_size,
                         GError **error)
{
	g_autoptr(GBytes) p_fw_bin = NULL;
	const guint8 *p_fw_bin_buf = NULL;
	guint8 *p_cur_page = NULL;
	guint page_index = 0;
	gsize fw_bin_buf_size = 0;
	gsize data_offset = 0;

	/* Check arguments */
	g_return_val_if_fail(FU_IS_ELAN_TS_FIRMWARE(self), FALSE);
	g_return_val_if_fail(p_page_buf != NULL, FALSE);

	/* Retrieve the binary payload set during parse */
	p_fw_bin = fu_firmware_get_bytes(FU_FIRMWARE(self), error);
	if (p_fw_bin == NULL)
		return FALSE;
	p_fw_bin_buf = g_bytes_get_data(p_fw_bin, &fw_bin_buf_size);

	/* Copy requested pages into the provided buffer */
	for (page_index = 0; page_index < page_count; page_index++) {
		data_offset = (gsize)(base_page_index + page_index) * ELAN_TS_FW_PAGE_SIZE;
		p_cur_page = p_page_buf + (page_index * ELAN_TS_FW_PAGE_SIZE);

		/* Make sure the read remains within binary boundaries */
		if (data_offset + ELAN_TS_FW_PAGE_SIZE > fw_bin_buf_size) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "requested page offset 0x%zx is outside firmware size 0x%zx",
				    data_offset,
				    fw_bin_buf_size);
			return FALSE;
		}

		/* Boundary check for the output buffer */
		if (((page_index + 1) * ELAN_TS_FW_PAGE_SIZE) > page_buf_size) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "destination buffer size %" G_GSIZE_FORMAT " is too small",
				    page_buf_size);
			return FALSE;
		}

		/* Copy the pre-formatted 132-byte page directly */
		memcpy(p_cur_page, p_fw_bin_buf + data_offset, ELAN_TS_FW_PAGE_SIZE);
	}

	return TRUE;
}

/**
 * elan_ts_firmware_get_remark_id:
 * @self: a #FuElanTsFirmware
 *
 * Gets the remark ID extracted from the firmware payload.
 * This ID is used to verify hardware compatibility before flashing.
 *
 * Returns: a remark ID, e.g. 0x1234
 **/
guint16
elan_ts_firmware_get_remark_id(FuElanTsFirmware *self)
{
	g_return_val_if_fail(FU_IS_ELAN_TS_FIRMWARE(self), 0xFFFF);
	return self->remark_id;
}

/**
 * elan_ts_firmware_parse_remark_id:
 * @p_fw_bin: (not nullable): firmware binary payload
 * @p_remark_id: (out): extracted remark ID
 * @error: (nullable): a #GError, or %NULL
 *
 * Extracts the Remark ID from the last page of the firmware binary.
 *
 * This function calculates the offset of the last page and verifies the
 * last page address header before reading the remark ID at the specific
 * parameter address. It supports both Gen5 and Gen6 series memory layouts.
 *
 * Returns: %TRUE for success, %FALSE if the binary is invalid or too small.
 **/
static gboolean
elan_ts_firmware_parse_remark_id(GBytes *p_fw_bin,
				  guint16 *p_remark_id,
				  GError **error)
{
	gsize   fw_bin_size      = 0;
	gsize   page_count       = 0;
	gsize   last_page_offset = 0;
	gsize   remark_id_offset = 0;
	gsize   offset_in_page   = 0;
	guint16 last_page_addr   = 0;
	guint16 base_addr        = 0;
	const guint8 *p_fw_bin_buf;

    /* Input Parameter Validation */
    g_return_val_if_fail(p_fw_bin != NULL, FALSE);
    g_return_val_if_fail(p_remark_id != NULL, FALSE);

    /* Get p_fw_bin_buf & Validate fw_bin_size */
	p_fw_bin_buf = g_bytes_get_data(p_fw_bin, &fw_bin_size);
	if (fw_bin_size < ELAN_TS_FW_PAGE_SIZE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "firmware binary too small (%" G_GSIZE_FORMAT " bytes)",
			    fw_bin_size);
		return FALSE;
	}

	/* Last Page Offset */
	page_count = (fw_bin_size + (ELAN_TS_FW_PAGE_SIZE - 1)) / ELAN_TS_FW_PAGE_SIZE;
	last_page_offset = (page_count - 1) * ELAN_TS_FW_PAGE_SIZE;
	
	/* Last Page Address (the first word of the last page) */
	if (!fu_memread_uint16_safe(p_fw_bin_buf, fw_bin_size, last_page_offset, &last_page_addr, G_LITTLE_ENDIAN, error)) {
		g_prefix_error(error, "failed to read last page address: ");
		return FALSE;
	}

	if ((last_page_addr != ELAN_TS_PARAM_ADDR_LAST_PAGE) &&
	    (last_page_addr != ELAN_TS_PARAM_ADDR_GEN5_LAST_PAGE)) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE,
			    "invalid last page address: 0x%04x", last_page_addr);
		return FALSE;
	}
	
	/* For Gen5 Series IC */
	base_addr = last_page_addr;
	if ((last_page_addr == ELAN_TS_PARAM_ADDR_GEN5_LAST_PAGE) &&
	    (ELAN_TS_PARAM_ADDR_REMARK_ID >= ELAN_TS_PARAM_ADDR_LAST_PAGE)) {
		base_addr = ELAN_TS_PARAM_ADDR_LAST_PAGE;
	}
	
	/* Calculate Remark ID offset (1 word of page address) */
	offset_in_page = (1 + (ELAN_TS_PARAM_ADDR_REMARK_ID - base_addr)) * 2;
	remark_id_offset = last_page_offset + offset_in_page;
	
	/* Read Remark ID */
	if (!fu_memread_uint16_safe(p_fw_bin_buf,
				    fw_bin_size,
				    remark_id_offset,
				    p_remark_id,
				    G_LITTLE_ENDIAN,
				    error)) {
		g_prefix_error(error, "failed to read remark ID: ");
		return FALSE;
	}

	return TRUE;
}

/**
 * fu_elan_ts_firmware_export:
 * @firmware: a #FuFirmware
 * @flags: #FuFirmwareExportFlags
 * @bn: a #XbBuilderNode
 *
 * Exports the ELAN TS specific firmware properties to the XMLb builder.
 **/
static void
fu_elan_ts_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
    FuElanTsFirmware *self = FU_ELAN_TS_FIRMWARE(firmware);

    /* Export ELAN TS specific metadata */
    fu_xmlb_builder_insert_kx(bn, "fw_type", self->fw_type);
    fu_xmlb_builder_insert_kx(bn, "debug_setting", self->debug_setting);
    fu_xmlb_builder_insert_kx(bn, "bin_size", self->bin_size); 
    fu_xmlb_builder_insert_kx(bn, "remark_id", self->remark_id);
}

/**
 * fu_elan_ts_firmware_validate:
 * @firmware: a #FuFirmware
 * @stream: a #GInputStream
 * @offset: the offset in the stream
 * @error: a #GError, or %NULL
 * 
 * Validates the ELAN TS firmware binary header by verifying the GUID matches
 * {998DE210-1981-4EBF-874F-27BD6C264484}.
 * 
 * Returns: %TRUE if the header is valid, %FALSE otherwise.
 **/
static gboolean
fu_elan_ts_firmware_validate(FuFirmware *firmware,
			     GInputStream *stream,
			     gsize offset,
			     GError **error)
{
	/* validate the firmware binary header using the auto-generated struct parser */
	return fu_struct_elan_ts_fw_bin_header_lvfs_type1_validate_stream(stream, offset, error);
}

/**
 * fu_elan_ts_firmware_parse:
 * @firmware: a #FuFirmware
 * @stream: a #GInputStream
 * @flags: a #FuFirmwareParseFlags
 * @error: a #GError, or %NULL
 * 
 * Parses the ELAN TS firmware binary, performs SHA-256 integrity check, and 
 * extracts the Remark ID from the payload.
 * 
 * Returns: %TRUE for success, %FALSE otherwise.
 **/
static gboolean
fu_elan_ts_firmware_parse(FuFirmware *firmware,
			  GInputStream *stream,
			  FuFirmwareParseFlags flags,
			  GError **error)
{
	FuElanTsFirmware *self = FU_ELAN_TS_FIRMWARE(firmware);
	g_autoptr(FuStructElanTsFwBinHeaderLvfsType1) p_st_header = NULL;
	g_autoptr(GBytes) p_fw_bin = NULL;
	g_autoptr(GString) p_security_code_gstr = g_string_new(NULL);
	g_autofree gchar *p_security_code_str = NULL;
	g_autofree gchar *p_fw_bin_sha256_str = NULL;
	gsize offset = 0;
	gsize index = 0;
	gsize security_code_buf_size = 0;
	const guint8 *p_security_code_buf = NULL;

	/* Ensure the stream is at the beginning, as other plugins may have moved the cursor */
	if (!g_seekable_seek(G_SEEKABLE(stream), 0, G_SEEK_SET, NULL, error)) {
		g_prefix_error(error, "failed to seek to beginning: ");
		return FALSE;
	}

	/* Validate Firmware Stream using the internal validator */
	if (!fu_elan_ts_firmware_validate(firmware, stream, offset, error)) {
		return FALSE;
	}

	/* Parse FW BIN Header using auto-generated struct helpers */
	p_st_header = fu_struct_elan_ts_fw_bin_header_lvfs_type1_parse_stream(stream, offset, error);
	if (p_st_header == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "failed to get FW BIN Header.");
		return FALSE;
	}

        /* Initialize firmware metadata from header */
	self->fw_type = fu_struct_elan_ts_fw_bin_header_lvfs_type1_get_fw_type(p_st_header);
	self->debug_setting = (FuElanTsDebugSetting)fu_struct_elan_ts_fw_bin_header_lvfs_type1_get_debug(p_st_header);
	self->bin_size = fu_struct_elan_ts_fw_bin_header_lvfs_type1_get_bin_size(p_st_header);
	ELAN_TS_DEBUG(self->debug_setting, "%s: fw_type=0x%x, debug_setting=0x%x, bin_size=0x%x(%u).", \
	         G_STRFUNC, self->fw_type, self->debug_setting, self->bin_size, self->bin_size);

	/* Extract and verify Security Code (expected to be a SHA256 hash) */
	p_security_code_buf = fu_struct_elan_ts_fw_bin_header_lvfs_type1_get_security_code(p_st_header, &security_code_buf_size);
	if ((p_security_code_buf == NULL) || (security_code_buf_size != sizeof(self->security_code))) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE,
			    "invalid security code size: expected %u, got %" G_GSIZE_FORMAT,
			    (guint)sizeof(self->security_code), security_code_buf_size);
		return FALSE;
	}
	memcpy(self->security_code, p_security_code_buf, sizeof(self->security_code));

	for (index = 0; index < sizeof(self->security_code); index++) {
		g_string_append_printf(p_security_code_gstr, "%02x", self->security_code[index]);
	}
	p_security_code_str = g_string_free(g_steal_pointer(&p_security_code_gstr), FALSE);
	ELAN_TS_DEBUG(self->debug_setting, "%s: security_code=0x%s", G_STRFUNC, p_security_code_str);

	/* Read the raw FW BIN payload from the stream */
	p_fw_bin = fu_input_stream_read_bytes(stream,
                                              offset + FU_STRUCT_ELAN_TS_FW_BIN_HEADER_LVFS_TYPE1_SIZE, /* offset */
                                              self->bin_size, /* count */
                                              NULL,
                                              error);
	if (p_fw_bin == NULL) {
		g_prefix_error(error, "failed to read firmware binary: ");
		return FALSE;
	}

	/* Compute SHA256 of the actual payload to verify integrity */
	p_fw_bin_sha256_str = g_compute_checksum_for_bytes(G_CHECKSUM_SHA256, p_fw_bin);
	if (p_fw_bin_sha256_str == NULL)
	{
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "failed to compute sha256 checksum string of FW BIN data.");
		return FALSE;
	}
	ELAN_TS_DEBUG(self->debug_setting, "%s: fw_bin_sha256=0x%s", G_STRFUNC, p_fw_bin_sha256_str);

	/* Integrity Check: Compare header security code with computed hash */	
	if (g_strcmp0(p_security_code_str, p_fw_bin_sha256_str) != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "SHA256 mismatch! (security_code=0x%s, fw_bin_sha256=0x%s)",
			    p_security_code_str,
			    p_fw_bin_sha256_str);
		return FALSE;
	}
	
	/* Extract the Remark ID from the payload bytes */
	if (!elan_ts_firmware_parse_remark_id(p_fw_bin,
						 &self->remark_id,
						 error)) {
		g_prefix_error(error, "failed to parse remark ID: ");
		return FALSE;
	}
	ELAN_TS_DEBUG(self->debug_setting, "%s: remark_id=0x%x", G_STRFUNC, self->remark_id);

	/* Set the verified binary payload to the firmware object */
	fu_firmware_set_bytes(firmware, p_fw_bin);

	return TRUE;
}

/**
 * fu_elan_ts_firmware_init:
 * @self: a #FuElanTsFirmware
 *
 * Initializes the ELAN TS firmware object with default values and flags.
 **/
static void
fu_elan_ts_firmware_init(FuElanTsFirmware *self)
{
	/* Initialize internal state and hardware identifiers */
	self->fw_type = FU_ELAN_TS_FW_TYPE_UNKNOWN;
	self->debug_setting = FU_ELAN_TS_DEBUG_SETTING_NONE;
	self->remark_id = 0x0;

	/* Configure firmware capabilities and version presentation */
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_CHECKSUM);
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_STORED_SIZE);
	fu_firmware_set_version_format(FU_FIRMWARE(self), FWUPD_VERSION_FORMAT_HEX);
}

/**
 * fu_elan_ts_firmware_class_init:
 * @klass: a #FuElanTsFirmwareClass
 *
 * Initializes the FuElanTsFirmware class by overriding virtual functions.
 **/
static void
fu_elan_ts_firmware_class_init(FuElanTsFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);

	/* Assign firmware-specific virtual function implementations */
	firmware_class->validate = fu_elan_ts_firmware_validate;
	firmware_class->parse = fu_elan_ts_firmware_parse;
	firmware_class->export = fu_elan_ts_firmware_export;
}
