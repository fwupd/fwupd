/*
 * Copyright 2025 Harris Tai <harris_tai@pixart.com>
 * Copyright 2025 Micky Hsieh <micky_hsieh@pixart.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-pixart-tp-firmware.h"
#include "fu-pixart-tp-section.h"

struct _FuPixartTpFirmware {
	FuFirmware parent_instance;
	guint16 header_ver;
	guint16 ic_part_id;
	guint16 flash_sectors;
};

G_DEFINE_TYPE(FuPixartTpFirmware, fu_pixart_tp_firmware, FU_TYPE_FIRMWARE)

static gboolean
fu_pixart_tp_firmware_validate(FuFirmware *firmware,
			       GInputStream *stream,
			       gsize offset,
			       GError **error)
{
	return fu_struct_pixart_tp_firmware_hdr_validate_stream(stream, offset, error);
}

static gboolean
fu_pixart_tp_firmware_parse(FuFirmware *firmware,
			    GInputStream *stream,
			    FuFirmwareParseFlags flags,
			    GError **error)
{
	FuPixartTpFirmware *self = FU_PIXART_TP_FIRMWARE(firmware);
	gboolean saw_fw = FALSE;
	gboolean saw_param = FALSE;
	guint16 num_sections;
	gsize offset = 0;
	g_autoptr(FuStructPixartTpFirmwareHdr) st_hdr = NULL;

	/* parse FWHD header via rustgen struct */
	st_hdr = fu_struct_pixart_tp_firmware_hdr_parse_stream(stream, 0, error);
	if (st_hdr == NULL)
		return FALSE;

	self->header_ver = fu_struct_pixart_tp_firmware_hdr_get_header_ver(st_hdr);
	fu_firmware_set_version_raw(firmware,
				    fu_struct_pixart_tp_firmware_hdr_get_file_ver(st_hdr));
	self->ic_part_id = fu_struct_pixart_tp_firmware_hdr_get_ic_part_id(st_hdr);
	num_sections = fu_struct_pixart_tp_firmware_hdr_get_num_sections(st_hdr);
	self->flash_sectors = fu_struct_pixart_tp_firmware_hdr_get_flash_sectors(st_hdr);

	/* header CRC check */
	if ((flags & FU_FIRMWARE_PARSE_FLAG_IGNORE_CHECKSUM) == 0) {
		guint32 stored = 0;
		guint32 calc = G_MAXUINT32;
		g_autoptr(GInputStream) partial_stream = NULL;

		if (!fu_input_stream_read_u32(stream,
					      FU_STRUCT_PIXART_TP_FIRMWARE_HDR_DEFAULT_HEADER_LEN -
						  4,
					      &stored,
					      G_LITTLE_ENDIAN,
					      error))
			return FALSE;

		partial_stream = fu_partial_input_stream_new(
		    stream,
		    0,
		    FU_STRUCT_PIXART_TP_FIRMWARE_HDR_DEFAULT_HEADER_LEN - 4,
		    error);
		if (partial_stream == NULL)
			return FALSE;
		if (!fu_input_stream_compute_crc32(partial_stream,
						   FU_CRC_KIND_B32_STANDARD,
						   &calc,
						   error))
			return FALSE;
		if (stored != calc) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "header CRC mismatch, got 0x%08x and expected 0x%08x",
				    calc,
				    stored);
			return FALSE;
		}
	}

	offset += FU_STRUCT_PIXART_TP_FIRMWARE_HDR_SIZE;
	for (guint i = 0; i < num_sections; i++) {
		FuPixartTpUpdateType update_type;
		g_autoptr(FuPixartTpSection) section = fu_pixart_tp_section_new();

		/* load section */
		if (!fu_firmware_parse_stream(FU_FIRMWARE(section), stream, offset, flags, error))
			return FALSE;
		update_type = fu_pixart_tp_section_get_update_type(section);
		if (update_type == FU_PIXART_TP_UPDATE_TYPE_FW_SECTION) {
			if (!fu_pixart_tp_section_has_flag(section,
							   FU_PIXART_TP_FIRMWARE_FLAG_VALID)) {
				g_set_error_literal(error,
						    FWUPD_ERROR,
						    FWUPD_ERROR_INVALID_FILE,
						    "firmware section marked invalid");
				return FALSE;
			}
			saw_fw = TRUE;
			fu_firmware_set_id(FU_FIRMWARE(section), FU_FIRMWARE_ID_PAYLOAD);

		} else if (update_type == FU_PIXART_TP_UPDATE_TYPE_PARAM) {
			if (!fu_pixart_tp_section_has_flag(section,
							   FU_PIXART_TP_FIRMWARE_FLAG_VALID)) {
				g_set_error_literal(error,
						    FWUPD_ERROR,
						    FWUPD_ERROR_INVALID_FILE,
						    "parameter section marked invalid");
				return FALSE;
			}
			saw_param = TRUE;
			fu_firmware_set_id(FU_FIRMWARE(section), "parameter");

		} else if (update_type == FU_PIXART_TP_UPDATE_TYPE_TF_FORCE) {
			fu_firmware_set_id(FU_FIRMWARE(section), "tf-force");
		}

		if (!fu_firmware_add_image(firmware, FU_FIRMWARE(section), error))
			return FALSE;
		offset += FU_STRUCT_PIXART_TP_FIRMWARE_SECTION_HDR_SIZE;
	}

	/* required section checks */
	if (!saw_fw) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "missing firmware section");
		return FALSE;
	}
	if (!saw_param) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "missing parameter section");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gchar *
fu_pixart_tp_firmware_convert_version(FuFirmware *firmware, guint64 version_raw)
{
	return fu_version_from_uint16((guint16)version_raw,
				      fu_firmware_get_version_format(firmware));
}

static void
fu_pixart_tp_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuPixartTpFirmware *self = FU_PIXART_TP_FIRMWARE(firmware);
	fu_xmlb_builder_insert_kx(bn, "header_ver", self->header_ver);
	fu_xmlb_builder_insert_kx(bn, "ic_part_id", self->ic_part_id);
	fu_xmlb_builder_insert_kx(bn, "flash_sectors", self->flash_sectors);
}

static GByteArray *
fu_pixart_tp_firmware_write(FuFirmware *firmware, GError **error)
{
	FuPixartTpFirmware *self = FU_PIXART_TP_FIRMWARE(firmware);
	gsize offset = FU_STRUCT_PIXART_TP_FIRMWARE_HDR_DEFAULT_HEADER_LEN;
	g_autoptr(FuStructPixartTpFirmwareHdr) st = fu_struct_pixart_tp_firmware_hdr_new();
	g_autoptr(GPtrArray) imgs = fu_firmware_get_images(firmware);

	fu_struct_pixart_tp_firmware_hdr_set_header_ver(st, self->header_ver);
	fu_struct_pixart_tp_firmware_hdr_set_file_ver(st, fu_firmware_get_version_raw(firmware));
	fu_struct_pixart_tp_firmware_hdr_set_ic_part_id(st, self->ic_part_id);
	fu_struct_pixart_tp_firmware_hdr_set_flash_sectors(st, self->flash_sectors);

	/* add section headers */
	fu_struct_pixart_tp_firmware_hdr_set_num_sections(st, imgs->len);
	for (guint i = 0; i < imgs->len; i++) {
		FuFirmware *img = g_ptr_array_index(imgs, i);
		g_autoptr(GBytes) blob = NULL;

		fu_firmware_set_offset(img, offset);
		blob = fu_firmware_write(img, error);
		if (blob == NULL)
			return NULL;
		fu_byte_array_append_bytes(st->buf, blob);
		offset += fu_firmware_get_size(img);
	}
	fu_byte_array_set_size(st->buf, FU_STRUCT_PIXART_TP_FIRMWARE_HDR_DEFAULT_HEADER_LEN, 0x0);

	/* set header CRC */
	if (!fu_memwrite_uint32_safe(
		st->buf->data,
		st->buf->len,
		st->buf->len - 4,
		fu_crc32(FU_CRC_KIND_B32_STANDARD, st->buf->data, st->buf->len - 4),
		G_LITTLE_ENDIAN,
		error))
		return NULL;

	/* add section data */
	for (guint i = 0; i < imgs->len; i++) {
		FuFirmware *img = g_ptr_array_index(imgs, i);
		g_autoptr(GBytes) blob = NULL;

		blob = fu_firmware_get_bytes(img, error);
		if (blob == NULL)
			return NULL;
		fu_byte_array_append_bytes(st->buf, blob);
	}

	/* success */
	return g_steal_pointer(&st->buf);
}

static gboolean
fu_pixart_tp_firmware_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuPixartTpFirmware *self = FU_PIXART_TP_FIRMWARE(firmware);
	const gchar *tmp;
	guint64 tmp64 = 0;

	/* simple properties */
	tmp = xb_node_query_text(n, "header_ver", NULL);
	if (tmp != NULL) {
		if (!fu_strtoull(tmp, &tmp64, 0, G_MAXUINT16, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->header_ver = (guint16)tmp64;
	}
	tmp = xb_node_query_text(n, "ic_part_id", NULL);
	if (tmp != NULL) {
		if (!fu_strtoull(tmp, &tmp64, 0, G_MAXUINT16, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->ic_part_id = (guint16)tmp64;
	}
	tmp = xb_node_query_text(n, "flash_sectors", NULL);
	if (tmp != NULL) {
		if (!fu_strtoull(tmp, &tmp64, 0, G_MAXUINT16, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->flash_sectors = (guint16)tmp64;
	}

	/* success */
	return TRUE;
}

FuPixartTpSection *
fu_pixart_tp_firmware_find_section_by_type(FuPixartTpFirmware *self,
					   FuPixartTpUpdateType update_type,
					   GError **error)
{
	g_autoptr(GPtrArray) secs = fu_firmware_get_images(FU_FIRMWARE(self));
	for (guint i = 0; i < secs->len; i++) {
		FuPixartTpSection *section = FU_PIXART_TP_SECTION(g_ptr_array_index(secs, i));
		if (fu_pixart_tp_section_get_update_type(section) == update_type &&
		    fu_pixart_tp_section_has_flag(section, FU_PIXART_TP_FIRMWARE_FLAG_VALID))
			return section;
	}
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_FOUND,
		    "cannot find section of type %u",
		    (guint)update_type);
	return NULL;
}

static void
fu_pixart_tp_firmware_init(FuPixartTpFirmware *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_CHECKSUM);
	fu_firmware_set_version_format(FU_FIRMWARE(self), FWUPD_VERSION_FORMAT_HEX);
	fu_firmware_set_images_max(FU_FIRMWARE(self), 8);
	g_type_ensure(FU_TYPE_PIXART_TP_SECTION);
}

static void
fu_pixart_tp_firmware_class_init(FuPixartTpFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->validate = fu_pixart_tp_firmware_validate;
	firmware_class->parse = fu_pixart_tp_firmware_parse;
	firmware_class->export = fu_pixart_tp_firmware_export;
	firmware_class->write = fu_pixart_tp_firmware_write;
	firmware_class->build = fu_pixart_tp_firmware_build;
	firmware_class->convert_version = fu_pixart_tp_firmware_convert_version;
}

FuFirmware *
fu_pixart_tp_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_PIXART_TP_FIRMWARE, NULL));
}
