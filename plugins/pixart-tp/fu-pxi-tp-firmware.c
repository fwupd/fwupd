/*
 * Copyright 2025 Harris Tai <harris_tai@pixart.com>
 * Copyright 2025 Micky Hsieh <micky_hsieh@pixart.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-pxi-tp-firmware.h"
#include "fu-pxi-tp-section.h"
#include "fu-pxi-tp-struct.h"

/* ---- magic string ------------------------------------------------------- */
const char PXI_TP_MAGIC[5] = "FWHD";

#define PXI_TP_HEADER_V1_LEN 0x0218 /* header size for v1.0 */
#define PXI_TP_MAX_SECTIONS  8	    /* max number of section descriptors */
#define PXI_TP_SECTION_SIZE  64	    /* size of each section header */

struct _FuPxiTpFirmware {
	FuFirmware parent_instance;

	/* header fields (host-endian) */
	guint16 header_len;
	guint16 header_ver;
	guint16 file_ver;
	guint16 ic_part_id;
	guint16 flash_sectors;
	guint32 file_crc32;
	guint32 header_crc32;
	guint16 num_sections;
};

G_DEFINE_TYPE(FuPxiTpFirmware, fu_pxi_tp_firmware, FU_TYPE_FIRMWARE)

static gboolean
fu_pxi_tp_firmware_validate(FuFirmware *firmware,
			    GInputStream *stream,
			    gsize offset,
			    GError **error)
{
	gsize hdrsz = 0;
	guint16 hdrlen = 0;
	const guint8 *d = NULL;
	g_autoptr(GBytes) hdr = NULL;

	/* we only support offset 0; quiet FALSE on others */
	(void)firmware;
	if (offset != 0)
		return FALSE;

	/* read just the fixed header */
	hdr = fu_input_stream_read_bytes(stream, offset, PXI_TP_HEADER_V1_LEN, NULL, error);
	if (hdr == NULL)
		return FALSE;

	hdrsz = g_bytes_get_size(hdr);
	d = g_bytes_get_data(hdr, NULL);

	/* magic */
	if (memcmp(d + FU_PXI_TP_FW_HEADER_OFFSET_MAGIC, PXI_TP_MAGIC, 4) != 0)
		return FALSE;

	/* header length (LE16) */
	if (!fu_memread_uint16_safe(d,
				    hdrsz,
				    FU_PXI_TP_FW_HEADER_OFFSET_HEADER_LEN,
				    &hdrlen,
				    G_LITTLE_ENDIAN,
				    NULL)) {
		return FALSE;
	}

	if (hdrlen != PXI_TP_HEADER_V1_LEN)
		return FALSE;

	return TRUE;
}

static gboolean
fu_pxi_tp_firmware_parse(FuFirmware *firmware,
			 GInputStream *stream,
			 FuFirmwareParseFlags flags,
			 GError **error)
{
	FuPxiTpFirmware *self = FU_PXI_TP_FIRMWARE(firmware);
	const guint8 *d = NULL;
	gsize sz = 0;
	gboolean saw_fw = FALSE;
	gboolean saw_fw_valid = FALSE;
	gboolean saw_param = FALSE;
	gboolean saw_param_valid = FALSE;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(FuStructPxiTpFirmwareHdr) st_hdr = NULL;

	/* read entire blob once */
	fw = fu_input_stream_read_bytes(stream, 0, G_MAXSIZE, NULL, error);
	if (fw == NULL)
		return FALSE;

	d = g_bytes_get_data(fw, &sz);
	fu_firmware_set_bytes(firmware, fw);

	if (sz < PXI_TP_HEADER_V1_LEN) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "file too small for FWHD header");
		return FALSE;
	}

	/* parse FWHD header via rustgen struct */
	st_hdr = fu_struct_pxi_tp_firmware_hdr_parse(d, sz, 0, error);
	if (st_hdr == NULL)
		return FALSE;

	self->header_len = fu_struct_pxi_tp_firmware_hdr_get_header_len(st_hdr);
	self->header_ver = fu_struct_pxi_tp_firmware_hdr_get_header_ver(st_hdr);
	self->file_ver = fu_struct_pxi_tp_firmware_hdr_get_file_ver(st_hdr);
	self->ic_part_id = fu_struct_pxi_tp_firmware_hdr_get_ic_part_id(st_hdr);
	self->flash_sectors = fu_struct_pxi_tp_firmware_hdr_get_flash_sectors(st_hdr);
	self->file_crc32 = fu_struct_pxi_tp_firmware_hdr_get_file_crc32(st_hdr);
	self->num_sections = fu_struct_pxi_tp_firmware_hdr_get_num_sections(st_hdr);

	/* validate header size */
	if (self->header_len != PXI_TP_HEADER_V1_LEN || self->header_len > sz) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "invalid FWHD header length");
		return FALSE;
	}

	/* header crc located at (hdrlen - 4) */
	if ((flags & FU_FIRMWARE_PARSE_FLAG_IGNORE_CHECKSUM) == 0) {
		guint32 stored = 0;
		guint32 calc = 0;

		if (!fu_memread_uint32_safe(d,
					    sz,
					    self->header_len - 4,
					    &stored,
					    G_LITTLE_ENDIAN,
					    error)) {
			return FALSE;
		}

		calc = fu_crc32(FU_CRC_KIND_B32_STANDARD, d, self->header_len - 4);
		if (stored != calc) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_FILE,
					    "header CRC mismatch");
			return FALSE;
		}

		self->header_crc32 = stored;
	}

	/* payload crc */
	if ((flags & FU_FIRMWARE_PARSE_FLAG_IGNORE_CHECKSUM) == 0 && sz > self->header_len) {
		guint32 calc =
		    fu_crc32(FU_CRC_KIND_B32_STANDARD, d + self->header_len, sz - self->header_len);

		if (calc != self->file_crc32) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_FILE,
					    "payload CRC mismatch");
			return FALSE;
		}
	}

	/* parse section headers into FuPxiTpSection child images */
	if (self->num_sections > PXI_TP_MAX_SECTIONS) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "too many sections");
		return FALSE;
	}

	for (guint i = 0; i < self->num_sections; i++) {
		gsize off = FU_PXI_TP_FW_HEADER_OFFSET_SECTIONS_BASE +
			    i * FU_STRUCT_PXI_TP_FIRMWARE_SECTION_HDR_SIZE;
		const guint8 *sect_buf = NULL;
		gsize sect_bufsz = 0;
		FuPxiTpSection *s = NULL;
		FuPxiTpUpdateType update_type;

		if (off + FU_STRUCT_PXI_TP_FIRMWARE_SECTION_HDR_SIZE > sz) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_FILE,
					    "section header out of range");
			return FALSE;
		}

		sect_buf = d + off;
		sect_bufsz = sz - off;

		s = fu_pxi_tp_section_new();
		if (!fu_pxi_tp_section_process_descriptor(s, sect_buf, sect_bufsz, error)) {
			g_object_unref(s);
			return FALSE;
		}

		update_type = fu_pxi_tp_section_get_update_type(s);
		if (update_type == FU_PXI_TP_UPDATE_TYPE_FW_SECTION) {
			saw_fw = TRUE;
			if (fu_pxi_tp_section_has_flag(s, FU_PXI_TP_FIRMWARE_FLAG_VALID))
				saw_fw_valid = TRUE;
		} else if (update_type == FU_PXI_TP_UPDATE_TYPE_PARAM) {
			saw_param = TRUE;
			if (fu_pxi_tp_section_has_flag(s, FU_PXI_TP_FIRMWARE_FLAG_VALID))
				saw_param_valid = TRUE;
		} else if (update_type == FU_PXI_TP_UPDATE_TYPE_TF_FORCE) {
			/* used by FuPxiTpHapticDevice via fu_firmware_get_image_by_id() */
			fu_firmware_set_id(FU_FIRMWARE(s), "com.pixart.tf-force");
		}

		if (!fu_pxi_tp_section_attach_payload_stream(s, stream, sz, error)) {
			g_object_unref(s);
			return FALSE;
		}

		if (!fu_firmware_add_image(firmware, FU_FIRMWARE(s), error)) {
			g_object_unref(s);
			return FALSE;
		}

		g_object_unref(s);
	}

	/* required section checks */
	if (!saw_fw || !saw_fw_valid) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "missing or invalid firmware section");
		return FALSE;
	}

	if (!saw_param || !saw_param_valid) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "missing or invalid parameter section");
		return FALSE;
	}

	fu_firmware_set_version_raw(firmware, self->file_ver);
	fu_firmware_set_size(firmware, sz);
	return TRUE;
}

static gchar *
fu_pxi_tp_firmware_convert_version(FuFirmware *firmware, guint64 version_raw)
{
	return fu_version_from_uint16((guint16)version_raw,
				      fu_firmware_get_version_format(firmware));
}

static void
fu_pxi_tp_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuPxiTpFirmware *self = FU_PXI_TP_FIRMWARE(firmware);

	fu_xmlb_builder_insert_kv(bn, "magic", "FWHD");
	fu_xmlb_builder_insert_kx(bn, "header_version", self->header_ver);
	fu_xmlb_builder_insert_kx(bn, "file_version", self->file_ver);
	fu_xmlb_builder_insert_kx(bn, "ic_part_id", self->ic_part_id);
	fu_xmlb_builder_insert_kx(bn, "flash_sectors", self->flash_sectors);
	fu_xmlb_builder_insert_kx(bn, "num_sections", self->num_sections);

	fu_xmlb_builder_insert_kx(bn, "header_crc32", self->header_crc32);
	fu_xmlb_builder_insert_kx(bn, "file_crc32", self->file_crc32);
}

static GByteArray *
fu_pxi_tp_firmware_write(FuFirmware *firmware, GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GBytes) fw = NULL;

	fw = fu_firmware_get_bytes_with_patches(firmware, error);
	if (fw == NULL)
		return NULL;
	fu_byte_array_append_bytes(buf, fw);
	return g_steal_pointer(&buf);
}

static gboolean
fu_pxi_tp_firmware_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	(void)firmware;
	(void)n;
	(void)error;
	return TRUE;
}

static void
fu_pxi_tp_firmware_init(FuPxiTpFirmware *self)
{
	(void)self;
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_CHECKSUM);
	fu_firmware_set_version_format(FU_FIRMWARE(self), FWUPD_VERSION_FORMAT_HEX);
}

static void
fu_pxi_tp_firmware_finalize(GObject *obj)
{
	G_OBJECT_CLASS(fu_pxi_tp_firmware_parent_class)->finalize(obj);
}

static void
fu_pxi_tp_firmware_class_init(FuPxiTpFirmwareClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);

	object_class->finalize = fu_pxi_tp_firmware_finalize;

	firmware_class->validate = fu_pxi_tp_firmware_validate;
	firmware_class->parse = fu_pxi_tp_firmware_parse;
	firmware_class->export = fu_pxi_tp_firmware_export;
	firmware_class->write = fu_pxi_tp_firmware_write;
	firmware_class->build = fu_pxi_tp_firmware_build;
	firmware_class->convert_version = fu_pxi_tp_firmware_convert_version;
}

FuFirmware *
fu_pxi_tp_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_PXI_TP_FIRMWARE, NULL));
}

GPtrArray *
fu_pxi_tp_firmware_get_sections(FuPxiTpFirmware *self)
{
	g_return_val_if_fail(FU_IS_PXI_TP_FIRMWARE(self), NULL);
	return fu_firmware_get_images(FU_FIRMWARE(self));
}

static FuPxiTpSection *
fu_pxi_tp_firmware_find_section_by_type(FuPxiTpFirmware *self, FuPxiTpUpdateType update_type)
{
	GPtrArray *secs = fu_pxi_tp_firmware_get_sections(self);

	if (secs == NULL)
		return NULL;

	for (guint i = 0; i < secs->len; i++) {
		FuPxiTpSection *s = FU_PXI_TP_SECTION(g_ptr_array_index(secs, i));
		if (fu_pxi_tp_section_get_update_type(s) == update_type &&
		    fu_pxi_tp_section_has_flag(s, FU_PXI_TP_FIRMWARE_FLAG_VALID))
			return s;
	}
	g_debug("cannot find section of type %u", (guint)update_type);
	return NULL;
}

guint32
fu_pxi_tp_firmware_get_file_firmware_crc(FuPxiTpFirmware *self)
{
	FuPxiTpSection *s =
	    fu_pxi_tp_firmware_find_section_by_type(self, FU_PXI_TP_UPDATE_TYPE_FW_SECTION);
	guint32 crc = s != NULL ? fu_pxi_tp_section_get_section_crc(s) : 0;
	g_debug("file firmware CRC: 0x%08x", (guint)crc);
	return crc;
}

guint32
fu_pxi_tp_firmware_get_file_parameter_crc(FuPxiTpFirmware *self)
{
	FuPxiTpSection *s =
	    fu_pxi_tp_firmware_find_section_by_type(self, FU_PXI_TP_UPDATE_TYPE_PARAM);
	guint32 crc = s != NULL ? fu_pxi_tp_section_get_section_crc(s) : 0;
	g_debug("file parameter CRC: 0x%08x", (guint)crc);
	return crc;
}

guint32
fu_pxi_tp_firmware_get_firmware_address(FuPxiTpFirmware *self)
{
	FuPxiTpSection *s =
	    fu_pxi_tp_firmware_find_section_by_type(self, FU_PXI_TP_UPDATE_TYPE_FW_SECTION);
	return s != NULL ? fu_pxi_tp_section_get_target_flash_start(s) : 0;
}
