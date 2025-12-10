/*
 * Copyright 2025 Harris Tai <harris_tai@pixart.com>
 * Copyright 2025 Micky Hsieh <micky_hsieh@pixart.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-pxi-tp-firmware.h"
#include "fu-pxi-tp-fw-struct.h"
#include "fu-pxi-tp-struct.h"

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
	GPtrArray *sections; /* FuPxiTpSection*, free with g_free */
};

G_DEFINE_TYPE(FuPxiTpFirmware, fu_pxi_tp_firmware, FU_TYPE_FIRMWARE)

/* handy: short hexdump for export fields */
static gchar *
fu_pxi_tp_firmware_hexdump_slice(const guint8 *p, gsize n, gsize maxbytes)
{
	GString *g = NULL;
	gsize m = n < maxbytes ? n : maxbytes;

	if (m == 0)
		return g_strdup("");

	g = g_string_sized_new(m * 3);
	for (gsize i = 0; i < m; i++)
		g_string_append_printf(g, "%02X%s", p[i], (i + 1 == m) ? "" : " ");
	return g_string_free(g, FALSE);
}

/* ---------------------- FuFirmware vfuncs ------------------ */
/**
 * fu_pxi_tp_firmware_validate:
 * @firmware: the firmware container object
 * @stream: unused (fwupd vfunc signature)
 * @offset: vfunc signature; we only accept 0
 * @error: return location for a #GError, or %NULL
 *
 * quick sanity checks to determine whether a blob looks like a pixart
 * FWHD v1.0 container. verifies magic string and header length.
 *
 * Returns: %TRUE if the blob looks parseable, %FALSE otherwise.
 */
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

/**
 * fu_pxi_tp_firmware_parse:
 * @firmware: the firmware container object
 * @stream: input stream
 * @flags: parse flags
 * @error: return location for a #GError, or %NULL
 *
 * parses the FWHD v1.0 header, verifies header CRC32 and payload CRC32,
 * and decodes up to 8 section descriptors into a private array.
 *
 * Returns: %TRUE on success, %FALSE with @error set on failure.
 */
/* nocheck:memread */
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

	d = g_bytes_get_data(fw, NULL);
	sz = g_bytes_get_size(fw);

	fu_firmware_set_bytes(firmware, fw);

	if (sz < PXI_TP_HEADER_V1_LEN) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "file too small for FWHD header");
		return FALSE;
	}

	/* ---------------------------------------------------------------
	 * parse FWHD header via rustgen struct (bytes-based)
	 * --------------------------------------------------------------- */
	st_hdr = fu_struct_pxi_tp_firmware_hdr_parse(d, sz, 0, error);
	if (st_hdr == NULL)
		return FALSE;

	/* check magic */
	{
		const guint8 *magic = fu_struct_pxi_tp_firmware_hdr_get_magic(st_hdr, NULL);
		if (memcmp(magic, "FWHD", 4) != 0) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_FILE,
					    "invalid FWHD magic");
			return FALSE;
		}
	}

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

	/* ---------------------------------------------------------------
	 * parse section headers
	 * --------------------------------------------------------------- */
	if (self->sections == NULL)
		self->sections = g_ptr_array_new_with_free_func(g_free);
	else
		g_ptr_array_set_size(self->sections, 0);

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
		g_autoptr(FuStructPxiTpFirmwareSectionHdr) st_section_hdr = NULL;
		FuPxiTpSection *s = NULL;
		const guint8 *name_src = NULL;
		const guint8 *reserved_src = NULL;
		gsize name_bufsz = 0;
		gsize reserved_bufsz = 0;

		st_section_hdr = fu_struct_pxi_tp_firmware_section_hdr_parse(d, sz, off, error);
		if (st_section_hdr == NULL)
			return FALSE;

		s = g_new0(FuPxiTpSection, 1);

		s->update_type =
		    fu_struct_pxi_tp_firmware_section_hdr_get_update_type(st_section_hdr);
		s->update_info =
		    fu_struct_pxi_tp_firmware_section_hdr_get_update_info(st_section_hdr);
		s->is_valid_update = (s->update_info & PXI_TP_UI_VALID) != 0;
		s->is_external = (s->update_info & PXI_TP_UI_EXTERNAL) != 0;

		s->target_flash_start =
		    fu_struct_pxi_tp_firmware_section_hdr_get_target_flash_start(st_section_hdr);
		s->internal_file_start =
		    fu_struct_pxi_tp_firmware_section_hdr_get_internal_file_start(st_section_hdr);
		s->section_length =
		    fu_struct_pxi_tp_firmware_section_hdr_get_section_length(st_section_hdr);
		s->section_crc =
		    fu_struct_pxi_tp_firmware_section_hdr_get_section_crc(st_section_hdr);

		/* reserved: copy raw bytes into section struct */
		reserved_src = fu_struct_pxi_tp_firmware_section_hdr_get_shared(st_section_hdr,
										&reserved_bufsz);
		if (reserved_src != NULL && reserved_bufsz > 0) {
			/* copy min(shared_len, struct_len) */
			gsize copy_len = MIN((gsize)PXI_TP_S_RESERVED_LEN, reserved_bufsz);

			if (!fu_memcpy_safe(s->reserved,
					    sizeof s->reserved,
					    0, /* dst offset */
					    reserved_src,
					    reserved_bufsz,
					    0, /* src offset */
					    copy_len,
					    error)) {
				g_free(s);
				return FALSE;
			}
		}

		/* external_file_name[PXI_TP_S_EXTNAME_LEN + 1] */
		name_src =
		    fu_struct_pxi_tp_firmware_section_hdr_get_extname(st_section_hdr, &name_bufsz);
		if (name_src != NULL && name_bufsz > 0) {
			gsize copy_len = MIN((gsize)PXI_TP_S_EXTNAME_LEN, name_bufsz);
			if (!fu_memcpy_safe((guint8 *)s->external_file_name,
					    sizeof s->external_file_name,
					    0,
					    name_src,
					    name_bufsz,
					    0,
					    copy_len,
					    error)) {
				g_free(s);
				return FALSE;
			}
		}
		s->external_file_name[PXI_TP_S_EXTNAME_LEN] = '\0';

		if (s->update_type == FU_PXI_TP_UPDATE_TYPE_FW_SECTION) {
			saw_fw = TRUE;
			if (s->is_valid_update)
				saw_fw_valid = TRUE;
		} else if (s->update_type == FU_PXI_TP_UPDATE_TYPE_PARAM) {
			saw_param = TRUE;
			if (s->is_valid_update)
				saw_param_valid = TRUE;
		}

		g_ptr_array_add(self->sections, s);
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

static gchar *
fu_pxi_tp_firmware_update_info_to_flags(guint8 ui)
{
	GString *s = g_string_new(NULL);

	if (ui & PXI_TP_UI_VALID)
		g_string_append(s, "VALID|");
	if (ui & PXI_TP_UI_EXTERNAL)
		g_string_append(s, "EXTERNAL|");
	if (s->len > 0)
		s->str[s->len - 1] = '\0';

	return s->str[0] ? g_string_free(s, FALSE) : (g_string_free(s, TRUE), g_strdup("0"));
}

static void
fu_pxi_tp_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuPxiTpFirmware *self = FU_PXI_TP_FIRMWARE(firmware);
	g_autoptr(GBytes) fw = NULL;
	const guint8 *d = NULL;
	gsize sz = 0;
	(void)flags; /* fix no used warning */

	/* bytes for optional recompute / hexdumps */
	fw = fu_firmware_get_bytes_with_patches(firmware, NULL);
	if (fw != NULL) {
		d = g_bytes_get_data(fw, NULL);
		sz = g_bytes_get_size(fw);
	}

	/* top-level identity and sizes */
	fu_xmlb_builder_insert_kv(bn, "magic", "FWHD");
	fu_xmlb_builder_insert_kx(bn, "file_size", sz);
	fu_xmlb_builder_insert_kx(bn, "header_length", self->header_len);
	/* payload size = total - header_len, bound to 0 */
	fu_xmlb_builder_insert_kx(bn,
				  "payload_size",
				  (sz > self->header_len) ? (sz - self->header_len) : 0);

	/* header core fields */
	fu_xmlb_builder_insert_kx(bn, "header_version", self->header_ver);
	fu_xmlb_builder_insert_kx(bn, "file_version", self->file_ver);
	fu_xmlb_builder_insert_kx(bn, "ic_part_id", self->ic_part_id);
	fu_xmlb_builder_insert_kx(bn, "flash_sectors", self->flash_sectors);
	fu_xmlb_builder_insert_kx(bn, "num_sections", self->num_sections);

	/* crcs (stored) */
	fu_xmlb_builder_insert_kx(bn, "header_crc32", self->header_crc32);
	fu_xmlb_builder_insert_kx(bn, "file_crc32", self->file_crc32);

	/* crcs (recomputed) + status */
	if (d != NULL && self->header_len >= 4 && self->header_len <= sz) {
		guint32 hdr_crc_calc = fu_crc32(FU_CRC_KIND_B32_STANDARD, d, self->header_len - 4);
		fu_xmlb_builder_insert_kx(bn, "header_crc32_calc", hdr_crc_calc);
		fu_xmlb_builder_insert_kv(bn,
					  "header_crc32_ok",
					  (hdr_crc_calc == self->header_crc32) ? "true" : "false");
	}
	if (d != NULL && sz > self->header_len) {
		guint32 file_crc_calc =
		    fu_crc32(FU_CRC_KIND_B32_STANDARD, d + self->header_len, sz - self->header_len);
		fu_xmlb_builder_insert_kx(bn, "file_crc32_calc", file_crc_calc);
		fu_xmlb_builder_insert_kv(bn,
					  "file_crc32_ok",
					  (file_crc_calc == self->file_crc32) ? "true" : "false");
	}

	/* ranges (handy for quick eyeballing) */
	if (self->header_len <= sz) {
		fu_xmlb_builder_insert_kx(bn, "range_header_begin", 0);
		fu_xmlb_builder_insert_kx(bn, "range_header_end", self->header_len);
		fu_xmlb_builder_insert_kx(bn, "range_payload_begin", self->header_len);
		fu_xmlb_builder_insert_kx(bn, "range_payload_end", sz);
	}

	/* sections */
	for (guint i = 0; self->sections != NULL && i < self->sections->len; i++) {
		FuPxiTpSection *s = g_ptr_array_index(self->sections, i);
		g_autofree gchar *p_type = g_strdup_printf("section%u_update_type", i);
		g_autofree gchar *p_type_name = g_strdup_printf("section%u_update_type_name", i);
		g_autofree gchar *p_info = g_strdup_printf("section%u_update_info", i);
		g_autofree gchar *p_info_flags = g_strdup_printf("section%u_update_info_flags", i);
		g_autofree gchar *p_is_valid = g_strdup_printf("section%u_is_valid", i);
		g_autofree gchar *p_is_ext = g_strdup_printf("section%u_is_external", i);
		g_autofree gchar *p_flash_start =
		    g_strdup_printf("section%u_target_flash_start", i);
		g_autofree gchar *p_int_start = g_strdup_printf("section%u_internal_file_start", i);
		g_autofree gchar *p_len = g_strdup_printf("section%u_section_length", i);
		g_autofree gchar *p_crc = g_strdup_printf("section%u_section_crc", i);
		g_autofree gchar *p_crc_calc = g_strdup_printf("section%u_section_crc_calc", i);
		g_autofree gchar *p_crc_ok = g_strdup_printf("section%u_section_crc_ok", i);
		g_autofree gchar *p_in_range = g_strdup_printf("section%u_in_file_range", i);
		g_autofree gchar *p_extname = g_strdup_printf("section%u_external_file", i);
		g_autofree gchar *p_sample_hex = g_strdup_printf("section%u_sample_hex_0_32", i);
		g_autofree gchar *p_reserved_hex = g_strdup_printf("section%u_reserved_hex", i);
		g_autofree gchar *flags_str =
		    fu_pxi_tp_firmware_update_info_to_flags(s->update_info);

		fu_xmlb_builder_insert_kx(bn, p_type, s->update_type);
		fu_xmlb_builder_insert_kv(
		    bn,
		    p_type_name,
		    fu_pxi_tp_update_type_to_string((FuPxiTpUpdateType)s->update_type));
		fu_xmlb_builder_insert_kx(bn, p_info, s->update_info);
		fu_xmlb_builder_insert_kv(bn, p_info_flags, flags_str);

		/* bools readable as true/false */
		fu_xmlb_builder_insert_kv(bn, p_is_valid, s->is_valid_update ? "true" : "false");
		fu_xmlb_builder_insert_kv(bn, p_is_ext, s->is_external ? "true" : "false");

		fu_xmlb_builder_insert_kx(bn, p_flash_start, s->target_flash_start);
		fu_xmlb_builder_insert_kx(bn, p_int_start, s->internal_file_start);
		fu_xmlb_builder_insert_kx(bn, p_len, s->section_length);
		fu_xmlb_builder_insert_kx(bn, p_crc, s->section_crc);

		/* reserved bytes as hex (zeroed if not present) */
		{
			g_autofree gchar *rhex =
			    fu_pxi_tp_firmware_hexdump_slice(s->reserved,
							     sizeof s->reserved,
							     sizeof s->reserved);
			fu_xmlb_builder_insert_kv(bn, p_reserved_hex, rhex);
		}

		if (!s->is_external && s->is_valid_update && d != NULL) {
			const gboolean in_range =
			    (s->internal_file_start + s->section_length <= sz);
			fu_xmlb_builder_insert_kv(bn, p_in_range, in_range ? "true" : "false");
			if (in_range) {
				g_autofree gchar *hex = NULL;
				guint32 calc = fu_crc32(FU_CRC_KIND_B32_STANDARD,
							d + s->internal_file_start,
							s->section_length);
				fu_xmlb_builder_insert_kx(bn, p_crc_calc, calc);
				fu_xmlb_builder_insert_kv(bn,
							  p_crc_ok,
							  (calc == s->section_crc) ? "true"
										   : "false");

				hex = fu_pxi_tp_firmware_hexdump_slice(d + s->internal_file_start,
								       s->section_length,
								       32);
				fu_xmlb_builder_insert_kv(bn, p_sample_hex, hex);
			}
		} else {
			fu_xmlb_builder_insert_kv(bn, p_in_range, "n/a");
		}

		/* extname (already trimmed / NUL-terminated on C side) */
		fu_xmlb_builder_insert_kv(bn, p_extname, s->external_file_name);
	}
}

/**
 * fu_pxi_tp_firmware_write:
 * @firmware: the container object
 * @error: return location for a #GError, or %NULL
 *
 * Returns: a new #GByteArray on success, or %NULL on error.
 */
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
	/* suppress unused-parameter warnings; this build vfunc is intentionally empty */
	(void)firmware;
	(void)n;
	(void)error;
	return TRUE;
}

static void
fu_pxi_tp_firmware_init(FuPxiTpFirmware *self)
{
	self->sections = g_ptr_array_new_with_free_func(g_free);
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_CHECKSUM);
	fu_firmware_set_version_format(FU_FIRMWARE(self), FWUPD_VERSION_FORMAT_HEX);
}

static void
fu_pxi_tp_firmware_finalize(GObject *obj)
{
	FuPxiTpFirmware *self = FU_PXI_TP_FIRMWARE(obj);

	if (self->sections != NULL) {
		g_ptr_array_unref(self->sections);
		self->sections = NULL;
	}
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

/* -------------------------- getters ----------------------- */

guint16
fu_pxi_tp_firmware_get_header_version(FuPxiTpFirmware *self)
{
	g_return_val_if_fail(FU_IS_PXI_TP_FIRMWARE(self), 0);
	return self->header_ver;
}

guint16
fu_pxi_tp_firmware_get_ic_part_id(FuPxiTpFirmware *self)
{
	g_return_val_if_fail(FU_IS_PXI_TP_FIRMWARE(self), 0);
	return self->ic_part_id;
}

guint16
fu_pxi_tp_firmware_get_total_flash_sectors(FuPxiTpFirmware *self)
{
	g_return_val_if_fail(FU_IS_PXI_TP_FIRMWARE(self), 0);
	return self->flash_sectors;
}

guint16
fu_pxi_tp_firmware_get_num_valid_sections(FuPxiTpFirmware *self)
{
	g_return_val_if_fail(FU_IS_PXI_TP_FIRMWARE(self), 0);
	return self->num_sections;
}

const GPtrArray *
fu_pxi_tp_firmware_get_sections(FuPxiTpFirmware *self)
{
	g_return_val_if_fail(FU_IS_PXI_TP_FIRMWARE(self), NULL);
	return self->sections;
}

/* returns a segment of data according to the file address offset (GByteArray*; full transfer) */
GByteArray *
fu_pxi_tp_firmware_get_slice_by_file(FuPxiTpFirmware *self,
				     gsize file_address,
				     gsize len,
				     GError **error)
{
	gsize sz = 0;
	g_autoptr(GBytes) fw = NULL;

	fw = fu_firmware_get_bytes_with_patches(FU_FIRMWARE(self), error);
	if (fw == NULL)
		return NULL;

	(void)g_bytes_get_data(fw, &sz);

	if (len == 0)
		return g_byte_array_new();

	if (file_address >= sz || (guint64)file_address + (guint64)len > (guint64)sz) {
		g_debug("file slice out of range: off=%" G_GSIZE_FORMAT ", len=%" G_GSIZE_FORMAT
			", size=%" G_GSIZE_FORMAT,
			file_address,
			len,
			sz);
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "file slice out of range");
		return NULL;
	}

	{
		GBytes *child = g_bytes_new_from_bytes(fw, file_address, len);
		gsize out_len = 0;
		guint8 *mem = g_bytes_unref_to_data(child, &out_len);
		return g_byte_array_new_take(mem, out_len);
	}
}

/* returns a segment of data according to the flash address offset (GByteArray*; full transfer) */
GByteArray *
fu_pxi_tp_firmware_get_slice_by_flash(FuPxiTpFirmware *self,
				      guint32 flash_addr,
				      gsize len,
				      GError **error)
{
	gsize sz = 0;
	const GPtrArray *secs = NULL;
	g_autoptr(GBytes) fw = NULL;

	fw = fu_firmware_get_bytes_with_patches(FU_FIRMWARE(self), error);
	if (fw == NULL)
		return NULL;

	(void)g_bytes_get_data(fw, &sz);

	if (len == 0)
		return g_byte_array_new();

	secs = fu_pxi_tp_firmware_get_sections(self);
	if (secs == NULL) {
		g_debug("no sections available when mapping flash slice");
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "no sections available");
		return NULL;
	}

	for (guint i = 0; i < secs->len; i++) {
		FuPxiTpSection *s = g_ptr_array_index((GPtrArray *)secs, i);
		guint64 sec_flash_begin;
		guint64 sec_flash_end;
		guint64 req_flash_begin;
		guint64 req_flash_end;

		if (!s->is_valid_update || s->is_external)
			continue;

		sec_flash_begin = (guint64)s->target_flash_start;
		sec_flash_end = sec_flash_begin + (guint64)s->section_length;
		req_flash_begin = (guint64)flash_addr;
		req_flash_end = req_flash_begin + (guint64)len;

		if (req_flash_begin >= sec_flash_begin && req_flash_end <= sec_flash_end) {
			guint64 off_in_sec = req_flash_begin - sec_flash_begin;
			guint64 file_off_64 = (guint64)s->internal_file_start + off_in_sec;

			if (file_off_64 + (guint64)len > (guint64)sz) {
				g_debug("mapped slice out of file range: sec=%u "
					"file_off=%" G_GUINT64_FORMAT " len=%" G_GSIZE_FORMAT
					" size=%" G_GSIZE_FORMAT,
					i,
					file_off_64,
					len,
					sz);
				g_set_error_literal(error,
						    FWUPD_ERROR,
						    FWUPD_ERROR_INVALID_FILE,
						    "mapped flash slice out of file range");
				return NULL;
			}

			{
				GBytes *child = g_bytes_new_from_bytes(fw, (gsize)file_off_64, len);
				gsize out_len = 0;
				guint8 *mem = g_bytes_unref_to_data(child, &out_len);
				return g_byte_array_new_take(mem, out_len);
			}
		}
	}

	g_debug("flash range [0x%08x..0x%08x) not covered by a single internal section",
		flash_addr,
		(guint32)(flash_addr + (len ? (len - 1) : 0)));
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "flash range not covered by a single internal section");
	return NULL;
}

/* find the first internal & valid section of the specified type; return NULL if not found */
static FuPxiTpSection *
fu_pxi_tp_firmware_find_section_by_type(FuPxiTpFirmware *self, guint8 type)
{
	const GPtrArray *secs = fu_pxi_tp_firmware_get_sections(self);

	if (secs == NULL)
		return NULL;

	for (guint i = 0; i < secs->len; i++) {
		FuPxiTpSection *s = g_ptr_array_index((GPtrArray *)secs, i);
		if (s->update_type == type)
			return s;
	}
	g_debug("cannot find section of type %u", type);
	return NULL;
}

guint32
fu_pxi_tp_firmware_get_file_firmware_crc(FuPxiTpFirmware *self)
{
	FuPxiTpSection *s =
	    fu_pxi_tp_firmware_find_section_by_type(self, FU_PXI_TP_UPDATE_TYPE_FW_SECTION);
	g_debug("file firmware CRC: 0x%08x", (guint)(s ? s->section_crc : 0));
	return s ? s->section_crc : 0;
}

guint32
fu_pxi_tp_firmware_get_file_parameter_crc(FuPxiTpFirmware *self)
{
	FuPxiTpSection *s =
	    fu_pxi_tp_firmware_find_section_by_type(self, FU_PXI_TP_UPDATE_TYPE_PARAM);
	g_debug("file parameter CRC: 0x%08x", (guint)(s ? s->section_crc : 0));
	return s ? s->section_crc : 0;
}

/* get the target_flash_start of the first section where update_type == FW_SECTION and is
 * internal & valid */
guint32
fu_pxi_tp_firmware_get_firmware_address(FuPxiTpFirmware *self)
{
	FuPxiTpSection *s =
	    fu_pxi_tp_firmware_find_section_by_type(self, FU_PXI_TP_UPDATE_TYPE_FW_SECTION);
	return s ? s->target_flash_start : 0;
}
