/*
 * Copyright 2026 Elan Microelectronics Corporation <paul.liang@emc.com.tw>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-elan-ts-common.h"
#include "fu-elan-ts-firmware.h"
#include "fu-elan-ts-struct.h"

struct _FuElanTsFirmware {
	FuFirmware parent_instance;
	FuElanTsFwType fw_type;
	FuElanTsDebugSetting debug_setting;
	guint8 security_code[32];
	guint32 bin_size;
	guint16 remark_id;
};

G_DEFINE_TYPE(FuElanTsFirmware, fu_elan_ts_firmware, FU_TYPE_FIRMWARE)

/**
 * fu_elan_ts_firmware_get_fw_type:
 * @self: a #FuElanTsFirmware
 *
 * Gets the firmware type specified in the binary header.
 *
 * Returns: a #FuElanTsFwType, e.g. %FU_ELAN_TS_FW_TYPE_EKT
 **/
FuElanTsFwType
fu_elan_ts_firmware_get_fw_type(FuElanTsFirmware *self)
{
	/* basic sanity check */
	g_return_val_if_fail(FU_IS_ELAN_TS_FIRMWARE(self), FU_ELAN_TS_FW_TYPE_UNKNOWN);

	return self->fw_type;
}

/**
 * fu_elan_ts_firmware_get_debug_setting:
 * @self: a #FuElanTsFirmware
 *
 * Gets the debug setting bitmask from the firmware header.
 * These settings control logging behavior and update policy overrides.
 *
 * Returns: a #FuElanTsDebugSetting bitmask
 **/
FuElanTsDebugSetting
fu_elan_ts_firmware_get_debug_setting(FuElanTsFirmware *self)
{
	/* basic sanity check */
	g_return_val_if_fail(FU_IS_ELAN_TS_FIRMWARE(self), FU_ELAN_TS_DEBUG_SETTING_NONE);

	return self->debug_setting;
}

/**
 * fu_elan_ts_firmware_get_bin_size:
 * @self: a #FuElanTsFirmware
 *
 * Returns: the size of fw_bin in bytes
 **/
guint32
fu_elan_ts_firmware_get_bin_size(FuElanTsFirmware *self)
{
	/* basic sanity check */
	g_return_val_if_fail(FU_IS_ELAN_TS_FIRMWARE(self), 0);

	return self->bin_size;
}

/**
 * fu_elan_ts_firmware_get_page_count:
 * @self: a #FuElanTsFirmware
 *
 * Returns: the number of pages
 **/
guint
fu_elan_ts_firmware_get_page_count(FuElanTsFirmware *self)
{
	/* basic sanity check */
	g_return_val_if_fail(FU_IS_ELAN_TS_FIRMWARE(self), 0);

	/* ensure the binary size is valid and properly page-aligned */
	g_return_val_if_fail((self->bin_size % ELAN_TS_FW_PAGE_SIZE) == 0, 0);

	/*
	 * Each page in the binary is 132 bytes (2 Addr + 128 Data + 2 Checksum).
	 * Safe conversion to guint after validation.
	 */
	return (guint)(self->bin_size / ELAN_TS_FW_PAGE_SIZE);
}

/**
 * fu_elan_ts_firmware_get_page:
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
fu_elan_ts_firmware_get_page(FuElanTsFirmware *self,
			     guint base_page_index,
			     guint page_count,
			     guint8 *p_page_buf,
			     gsize page_buf_size,
			     GError **error)
{
	g_autoptr(GBytes) p_fw_bin = NULL;
	const guint8 *p_fw_bin_buf = NULL;
	guint page_index = 0;
	gsize fw_bin_buf_size = 0;
	gsize data_offset = 0;
	gsize cur_page_offset = 0;
	gsize total_page_count = 0;

	/* basic sanity checks */
	g_return_val_if_fail(FU_IS_ELAN_TS_FIRMWARE(self), FALSE);
	g_return_val_if_fail(p_page_buf != NULL, FALSE);
	g_return_val_if_fail((error == NULL) || (*error == NULL), FALSE);

	/* retrieve the binary payload set during parse */
	p_fw_bin = fu_firmware_get_bytes(FU_FIRMWARE(self), error);
	if (p_fw_bin == NULL)
		return FALSE;
	p_fw_bin_buf = g_bytes_get_data(p_fw_bin, &fw_bin_buf_size);

	/* validate requested page range upfront to prevent integer wrap-around */
	total_page_count = fw_bin_buf_size / ELAN_TS_FW_PAGE_SIZE;
	if (((gsize)base_page_index > total_page_count) ||
	    ((gsize)page_count > (total_page_count - (gsize)base_page_index))) {
		g_set_error(
		    error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_INVALID_DATA,
		    "requested page range [%u, %u) exceeds total page count %" G_GSIZE_FORMAT,
		    base_page_index,
		    base_page_index + page_count,
		    total_page_count);
		return FALSE;
	}

	/* copy requested pages into the provided buffer */
	for (page_index = 0; page_index < page_count; page_index++) {
		data_offset = (gsize)(base_page_index + page_index) * ELAN_TS_FW_PAGE_SIZE;
		cur_page_offset = page_index * ELAN_TS_FW_PAGE_SIZE;

		/* make sure the read remains within binary boundaries */
		if (data_offset + ELAN_TS_FW_PAGE_SIZE > fw_bin_buf_size) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "requested page offset 0x%zx is outside firmware size 0x%zx",
				    data_offset,
				    fw_bin_buf_size);
			return FALSE;
		}

		/* boundary check for the output buffer */
		if (((page_index + 1) * ELAN_TS_FW_PAGE_SIZE) > page_buf_size) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "destination buffer size %" G_GSIZE_FORMAT " is too small",
				    page_buf_size);
			return FALSE;
		}

		/* copy the pre-formatted 132-byte page directly */
		if (!fu_memcpy_safe(p_page_buf,
				    page_buf_size,
				    cur_page_offset,
				    p_fw_bin_buf,
				    fw_bin_buf_size,
				    data_offset,
				    ELAN_TS_FW_PAGE_SIZE,
				    error)) {
			g_prefix_error(error, "failed to copy page %u: ", page_index);
			return FALSE;
		}
	}

	return TRUE;
}

/**
 * fu_elan_ts_firmware_get_remark_id:
 * @self: a #FuElanTsFirmware
 *
 * Gets the remark ID extracted from the firmware payload.
 * This ID is used to verify hardware compatibility before flashing.
 *
 * Returns: a remark ID, e.g. 0x1234
 **/
guint16
fu_elan_ts_firmware_get_remark_id(FuElanTsFirmware *self)
{
	/* basic sanity check */
	g_return_val_if_fail(FU_IS_ELAN_TS_FIRMWARE(self), G_MAXUINT16);

	return self->remark_id;
}

/**
 * fu_elan_ts_firmware_parse_remark_id:
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
fu_elan_ts_firmware_parse_remark_id(GBytes *p_fw_bin, guint16 *p_remark_id, GError **error)
{
	gsize fw_bin_size = 0;
	gsize page_count = 0;
	gsize last_page_offset = 0;
	gsize remark_id_offset = 0;
	gsize offset_in_page = 0;
	guint16 last_page_addr = 0;
	guint16 base_addr = 0;
	const guint8 *p_fw_bin_buf;

	/* basic sanity checks */
	g_return_val_if_fail(p_fw_bin != NULL, FALSE);
	g_return_val_if_fail(p_remark_id != NULL, FALSE);
	g_return_val_if_fail((error == NULL) || (*error == NULL), FALSE);

	/* get p_fw_bin_buf & Validate fw_bin_size */
	p_fw_bin_buf = g_bytes_get_data(p_fw_bin, &fw_bin_size);

	/* Reject non page-aligned or empty binaries
	 * ensures a full last page to read remark ID */
	if ((fw_bin_size == 0) || ((fw_bin_size % ELAN_TS_FW_PAGE_SIZE) != 0)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "firmware binary size %" G_GSIZE_FORMAT " is not a multiple of %u",
			    fw_bin_size,
			    (guint)ELAN_TS_FW_PAGE_SIZE);
		return FALSE;
	}

	/* last Page Offset */
	page_count = fw_bin_size / ELAN_TS_FW_PAGE_SIZE;
	last_page_offset = (page_count - 1) * ELAN_TS_FW_PAGE_SIZE;

	/* last Page Address (the first word of the last page) */
	if (!fu_memread_uint16_safe(p_fw_bin_buf,
				    fw_bin_size,
				    last_page_offset,
				    &last_page_addr,
				    G_LITTLE_ENDIAN,
				    error)) {
		g_prefix_error_literal(error, "failed to read last page address: ");
		return FALSE;
	}

	if ((last_page_addr != ELAN_TS_PARAM_ADDR_LAST_PAGE) &&
	    (last_page_addr != ELAN_TS_PARAM_ADDR_GEN5_LAST_PAGE)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "invalid last page address: 0x%04x",
			    last_page_addr);
		return FALSE;
	}

	/* for Gen5 Series IC */
	base_addr = last_page_addr;
	if (last_page_addr == ELAN_TS_PARAM_ADDR_GEN5_LAST_PAGE)
		base_addr = ELAN_TS_PARAM_ADDR_LAST_PAGE;

	/* calculate Remark ID offset (1 word of page address) */
	offset_in_page = (1 + (ELAN_TS_PARAM_ADDR_REMARK_ID - base_addr)) * 2;
	remark_id_offset = last_page_offset + offset_in_page;

	/* read Remark ID */
	if (!fu_memread_uint16_safe(p_fw_bin_buf,
				    fw_bin_size,
				    remark_id_offset,
				    p_remark_id,
				    G_LITTLE_ENDIAN,
				    error)) {
		g_prefix_error_literal(error, "failed to read remark ID: ");
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

	/* export ELAN TS specific metadata */
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
	/* basic sanity checks */
	g_return_val_if_fail(FU_IS_FIRMWARE(firmware), FALSE);
	g_return_val_if_fail((error == NULL) || (*error == NULL), FALSE);

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
	g_autoptr(FuStructElanTsFwBinHeaderLvfsType1) st_header = NULL;
	g_autoptr(GBytes) p_fw_bin = NULL;
	g_autoptr(GString) p_security_code_gstr = g_string_new(NULL);
	g_autofree gchar *p_security_code_str = NULL;
	g_autofree gchar *p_fw_bin_sha256_str = NULL;
	gsize offset = 0;
	gsize index = 0;
	gsize security_code_buf_size = 0;
	const guint8 *p_security_code_buf = NULL;

	/* basic sanity checks */
	g_return_val_if_fail(FU_IS_FIRMWARE(firmware), FALSE);
	g_return_val_if_fail((error == NULL) || (*error == NULL), FALSE);

	/* ensure the stream is at the beginning, as other plugins may have moved the cursor */
	if (!g_seekable_seek(G_SEEKABLE(stream), 0, G_SEEK_SET, NULL, error)) {
		g_prefix_error_literal(error, "failed to seek to beginning: ");
		return FALSE;
	}

	/* validate Firmware Stream using the internal validator */
	if (!fu_elan_ts_firmware_validate(firmware, stream, offset, error))
		return FALSE;

	/* parse FW BIN Header using auto-generated struct helpers */
	st_header = fu_struct_elan_ts_fw_bin_header_lvfs_type1_parse_stream(stream, offset, error);
	if (st_header == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "failed to get FW BIN Header");
		return FALSE;
	}

	/* initialize firmware metadata from header */
	self->fw_type = fu_struct_elan_ts_fw_bin_header_lvfs_type1_get_fw_type(st_header);
	self->debug_setting =
	    (FuElanTsDebugSetting)fu_struct_elan_ts_fw_bin_header_lvfs_type1_get_debug(st_header);
	self->bin_size = fu_struct_elan_ts_fw_bin_header_lvfs_type1_get_bin_size(st_header);
	g_debug("fw_type=0x%x, debug_setting=0x%x, bin_size=0x%x(%u)",
		self->fw_type,
		self->debug_setting,
		self->bin_size,
		self->bin_size);

	/* extract and verify Security Code (expected to be a SHA256 hash) */
	p_security_code_buf =
	    fu_struct_elan_ts_fw_bin_header_lvfs_type1_get_security_code(st_header,
									 &security_code_buf_size);
	if ((p_security_code_buf == NULL) ||
	    (security_code_buf_size != sizeof(self->security_code))) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "invalid security code size: expected %u, got %" G_GSIZE_FORMAT,
			    (guint)sizeof(self->security_code),
			    security_code_buf_size);
		return FALSE;
	}

	if (!fu_memcpy_safe(self->security_code,
			    sizeof(self->security_code),
			    0, /* dst */
			    p_security_code_buf,
			    security_code_buf_size,
			    0, /* src */
			    security_code_buf_size,
			    error)) {
		g_prefix_error_literal(error, "failed to copy security code: ");
		return FALSE;
	}

	for (index = 0; index < sizeof(self->security_code); index++)
		g_string_append_printf(p_security_code_gstr, "%02x", self->security_code[index]);
	p_security_code_str = g_string_free(g_steal_pointer(&p_security_code_gstr), FALSE);
	g_debug("security_code=0x%s", p_security_code_str);

	/* read the raw FW BIN payload from the stream */
	p_fw_bin = fu_input_stream_read_bytes(
	    stream,
	    offset + FU_STRUCT_ELAN_TS_FW_BIN_HEADER_LVFS_TYPE1_SIZE, /* offset */
	    self->bin_size,					      /* count */
	    NULL,
	    error);
	if (p_fw_bin == NULL) {
		g_prefix_error_literal(error, "failed to read firmware binary: ");
		return FALSE;
	}

	/* compute SHA256 of the actual payload to verify integrity */
	p_fw_bin_sha256_str = g_compute_checksum_for_bytes(G_CHECKSUM_SHA256, p_fw_bin);
	if (p_fw_bin_sha256_str == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "failed to compute sha256 checksum string of FW BIN data");
		return FALSE;
	}
	g_debug("fw_bin_sha256=0x%s", p_fw_bin_sha256_str);

	/* integrity Check: Compare header security code with computed hash */
	if (g_strcmp0(p_security_code_str, p_fw_bin_sha256_str) != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "SHA256 mismatch! (security_code=0x%s, fw_bin_sha256=0x%s)",
			    p_security_code_str,
			    p_fw_bin_sha256_str);
		return FALSE;
	}

	/* parse the Remark ID from the payload bytes */
	if (!fu_elan_ts_firmware_parse_remark_id(p_fw_bin, &self->remark_id, error)) {
		g_prefix_error_literal(error, "failed to parse remark ID: ");
		return FALSE;
	}
	g_debug("remark_id=0x%x", self->remark_id);

	/* set the verified binary payload to the firmware object */
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
	/* initialize internal state and hardware identifiers */
	self->fw_type = FU_ELAN_TS_FW_TYPE_UNKNOWN;
	self->debug_setting = FU_ELAN_TS_DEBUG_SETTING_NONE;
	self->remark_id = 0x0;

	/* configure firmware capabilities and version presentation */
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

	/* assign firmware-specific virtual function implementations */
	firmware_class->validate = fu_elan_ts_firmware_validate;
	firmware_class->parse = fu_elan_ts_firmware_parse;
	firmware_class->export = fu_elan_ts_firmware_export;
}
