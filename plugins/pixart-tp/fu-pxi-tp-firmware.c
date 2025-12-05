/*
 * Copyright 2025 Harris Tai <harris_tai@pixart.com>
 * Copyright 2025 Micky Hsieh <micky_hsieh@pixart.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-pxi-tp-common.h"
#include "fu-pxi-tp-firmware.h"
#include "fu-pxi-tp-fw-struct.h"

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
	gsize m = n < maxbytes ? n : maxbytes;
	GString *g = NULL;

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
 * Quick sanity checks to determine whether a blob looks like a PixArt
 * FWHD v1.0 container. Verifies magic string and header length.
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

	/* we only support offset 0; quiet FALSE on others */
	if (offset != 0)
		return FALSE;

	/* read just the fixed header */
	g_autoptr(GBytes) hdr =
	    fu_input_stream_read_bytes(stream, offset, PXI_TP_HEADER_V1_LEN, NULL, error);
	if (hdr == NULL)
		return FALSE;

	hdrsz = g_bytes_get_size(hdr);
	d = g_bytes_get_data(hdr, NULL);

	/* magic */
	if (memcmp(d + PXI_TP_O_MAGIC, PXI_TP_MAGIC, 4) != 0)
		return FALSE;

	/* header length (LE16) */
	if (!fu_memread_uint16_safe(d, hdrsz, PXI_TP_O_HDRLEN, &hdrlen, G_LITTLE_ENDIAN, NULL))
		return FALSE;

	if (hdrlen != PXI_TP_HEADER_V1_LEN)
		return FALSE;

	return TRUE;
}

/**
 * fu_pxi_tp_firmware_parse:
 * @firmware: the firmware container object
 * @stream: unused (fwupd vfunc signature)
 * @flags: parse flags
 * @error: return location for a #GError, or %NULL
 *
 * Parses the FWHD v1.0 header, verifies header CRC32 and payload CRC32,
 * and decodes up to 8 section descriptors into a private array. For
 * internal sections, we trust `internal_file_start` as an absolute file
 * offset from the beginning of the blob (already including header).
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
	const guint8 *d = NULL;
	gsize sz = 0;
	guint64 sectab_end = 0;
	FuPxiTpFirmware *self = FU_PXI_TP_FIRMWARE(firmware);

	/* read the entire blob through the fwupd streaming helper */
	g_autoptr(GBytes) fw = fu_input_stream_read_bytes(stream, 0, G_MAXSIZE, NULL, error);
	if (fw == NULL)
		return FALSE;

	/* store original bytes for later (patching/export/slices) */
	fu_firmware_set_bytes(firmware, fw);

	d = g_bytes_get_data(fw, NULL);
	sz = g_bytes_get_size(fw);

	/* lightweight checks again (with errors reported here) */
	if (sz < PXI_TP_HEADER_V1_LEN) {
		g_debug("file too small for header: %" G_GSIZE_FORMAT " bytes (need >= %u)",
			sz,
			(guint)PXI_TP_HEADER_V1_LEN);
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "file too small for FWHD header");
		return FALSE;
	}
	if (memcmp(d + PXI_TP_O_MAGIC, PXI_TP_MAGIC, 4) != 0) {
		g_debug("bad magic, expected 'FWHD'");
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "invalid FWHD magic");
		return FALSE;
	}

	/* --- parse fixed header (all via safe readers) --- */
	if (!fu_memread_uint16_safe(d,
				    sz,
				    PXI_TP_O_HDRLEN,
				    &self->header_len,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	if (self->header_len != PXI_TP_HEADER_V1_LEN || self->header_len > sz ||
	    self->header_len < 4) {
		g_debug("header length error: header_len=%u, file_size=%" G_GSIZE_FORMAT,
			(guint)self->header_len,
			sz);
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "invalid FWHD header length");
		return FALSE;
	}

	if (!fu_memread_uint16_safe(d,
				    sz,
				    PXI_TP_O_HDRVER,
				    &self->header_ver,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	if (!fu_memread_uint16_safe(d,
				    sz,
				    PXI_TP_O_FILEVER,
				    &self->file_ver,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	if (!fu_memread_uint16_safe(d,
				    sz,
				    PXI_TP_O_PARTID,
				    &self->ic_part_id,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	if (!fu_memread_uint16_safe(d,
				    sz,
				    PXI_TP_O_SECTORS,
				    &self->flash_sectors,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	if (!fu_memread_uint32_safe(d,
				    sz,
				    PXI_TP_O_TOTALCRC,
				    &self->file_crc32,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	if (!fu_memread_uint16_safe(d,
				    sz,
				    PXI_TP_O_NUMSECTIONS,
				    &self->num_sections,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;

	/* header CRC is at the tail of header: PXI_TP_O_HDRCRC(header_len) */
	if (!fu_memread_uint32_safe(d,
				    sz,
				    pxi_tp_o_hdrcrc(self->header_len),
				    &self->header_crc32,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;

	if (sz < self->header_len) {
		g_debug("truncated header: file_size=%" G_GSIZE_FORMAT ", header_len=%u",
			sz,
			(guint)self->header_len);
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "truncated FWHD header");
		return FALSE;
	}
	if (self->num_sections > PXI_TP_MAX_SECTIONS) {
		g_debug("num_sections %u exceeds max %u",
			(guint)self->num_sections,
			(guint)PXI_TP_MAX_SECTIONS);
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "too many FWHD sections");
		return FALSE;
	}

	/* --- verify header CRC32: [0 .. header_len-4) --- */
	if ((flags & FU_FIRMWARE_PARSE_FLAG_IGNORE_CHECKSUM) == 0) {
		guint32 hdr_crc_calc = fu_crc32(FU_CRC_KIND_B32_STANDARD, d, self->header_len - 4);
		if (hdr_crc_calc != self->header_crc32) {
			g_debug("header CRC mismatch, got 0x%08x, expected 0x%08x",
				(guint)hdr_crc_calc,
				(guint)self->header_crc32);
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_FILE,
					    "FWHD header CRC mismatch");
			return FALSE;
		}
	}

	/* --- verify payload CRC32: [header_len .. end) --- */
	if ((flags & FU_FIRMWARE_PARSE_FLAG_IGNORE_CHECKSUM) == 0 && sz > self->header_len) {
		guint32 file_crc_calc =
		    fu_crc32(FU_CRC_KIND_B32_STANDARD, d + self->header_len, sz - self->header_len);
		if (file_crc_calc != self->file_crc32) {
			g_debug("file CRC mismatch, got 0x%08x, expected 0x%08x",
				(guint)file_crc_calc,
				(guint)self->file_crc32);
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_FILE,
					    "FWHD payload CRC mismatch");
			return FALSE;
		}
	}

	/* --- parse section headers --- */
	if (self->sections != NULL)
		g_ptr_array_set_size(self->sections, 0);
	else
		self->sections = g_ptr_array_new_with_free_func(g_free);

	sectab_end = (guint64)PXI_TP_O_SECTIONS_BASE +
		     (guint64)self->num_sections * (guint64)PXI_TP_SECTION_SIZE;

	if (sectab_end > self->header_len) {
		g_debug("section table exceeds header: need %" G_GUINT64_FORMAT
			" bytes within header_len=%u",
			sectab_end - (guint64)PXI_TP_O_SECTIONS_BASE,
			(guint)self->header_len);
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "FWHD section table exceeds header");
		return FALSE;
	}

	/* track presence/validity of required section types */
	gboolean saw_fw = FALSE, saw_fw_valid = FALSE;
	gboolean saw_param = FALSE, saw_param_valid = FALSE;

	for (guint i = 0; i < self->num_sections; i++) {
		const guint64 sec_off =
		    (guint64)PXI_TP_O_SECTIONS_BASE + (guint64)i * PXI_TP_SECTION_SIZE;
		const guint8 *sec;
		FuPxiTpSection *s;
		gsize dstcap;
		gsize want;

		if (sec_off + PXI_TP_SECTION_SIZE > self->header_len) {
			g_debug("section %u header out of range", i);
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_FILE,
					    "FWHD section header out of range");
			return FALSE;
		}

		sec = d + PXI_TP_O_SECTIONS_BASE + i * PXI_TP_SECTION_SIZE;
		s = g_new0(FuPxiTpSection, 1);

		s->update_type = sec[PXI_TP_S_O_TYPE];
		s->update_info = sec[PXI_TP_S_O_INFO];
		s->is_valid_update = (s->update_info & PXI_TP_UI_VALID) != 0;
		s->is_external = (s->update_info & PXI_TP_UI_EXTERNAL) != 0;

		/* numeric fields via safe reads using absolute offsets into d */
		if (!fu_memread_uint32_safe(
			d,
			sz,
			(PXI_TP_O_SECTIONS_BASE + i * PXI_TP_SECTION_SIZE + PXI_TP_S_O_FLASHADDR),
			&s->target_flash_start,
			G_LITTLE_ENDIAN,
			error)) {
			g_free(s);
			return FALSE;
		}
		if (!fu_memread_uint32_safe(
			d,
			sz,
			(PXI_TP_O_SECTIONS_BASE + i * PXI_TP_SECTION_SIZE + PXI_TP_S_O_INTSTART),
			&s->internal_file_start,
			G_LITTLE_ENDIAN,
			error)) {
			g_free(s);
			return FALSE;
		}
		if (!fu_memread_uint32_safe(
			d,
			sz,
			(PXI_TP_O_SECTIONS_BASE + i * PXI_TP_SECTION_SIZE + PXI_TP_S_O_INTLEN),
			&s->section_length,
			G_LITTLE_ENDIAN,
			error)) {
			g_free(s);
			return FALSE;
		}
		/* section CRC at 0x0E */
		if (!fu_memread_uint32_safe(
			d,
			sz,
			(PXI_TP_O_SECTIONS_BASE + i * PXI_TP_SECTION_SIZE + PXI_TP_S_O_SECTCRC),
			&s->section_crc,
			G_LITTLE_ENDIAN,
			error)) {
			g_free(s);
			return FALSE;
		}

		/* new: copy Reserved (12 bytes at 0x12) */
		if (!fu_memcpy_safe((guchar *)s->reserved,
				    sizeof s->reserved,
				    0,
				    sec,
				    PXI_TP_SECTION_SIZE,
				    PXI_TP_S_O_RESERVED,
				    PXI_TP_S_O_RESERVED_LEN,
				    error)) {
			g_free(s);
			return FALSE;
		}

		/* copy fixed-width external filename (34 bytes, offset 0x1E) */
		dstcap = sizeof(s->external_file_name);
		want = MIN((gsize)PXI_TP_S_EXTNAME_LEN, (dstcap > 0 ? dstcap - 1 : 0));

		if (!fu_memcpy_safe((guchar *)s->external_file_name,
				    dstcap,
				    0,
				    sec,
				    PXI_TP_SECTION_SIZE,
				    PXI_TP_S_O_EXTNAME,
				    want,
				    error)) {
			g_free(s);
			return FALSE;
		}

		/* ensure NUL end */
		if (dstcap > 0)
			s->external_file_name[want] = '\0';

		/* known update types (0/1/2/3/16) */
		if (s->update_type != PXI_TP_UPDATE_TYPE_GENERAL &&
		    s->update_type != PXI_TP_UPDATE_TYPE_FW_SECTION &&
		    s->update_type != PXI_TP_UPDATE_TYPE_BOOTLOADER &&
		    s->update_type != PXI_TP_UPDATE_TYPE_PARAM &&
		    s->update_type != PXI_TP_UPDATE_TYPE_TF_FORCE) {
			g_debug("unknown update_type %u for section %u", s->update_type, i);
		}

		/* track required types (must exist and be VALID=1) */
		if (s->update_type == PXI_TP_UPDATE_TYPE_FW_SECTION) {
			saw_fw = TRUE;
			if (s->is_valid_update)
				saw_fw_valid = TRUE;
		} else if (s->update_type == PXI_TP_UPDATE_TYPE_PARAM) {
			saw_param = TRUE;
			if (s->is_valid_update)
				saw_param_valid = TRUE;
		}

		g_ptr_array_add(self->sections, s);
	}

	/* --- enforce required section presence & validity --- */
	if (!saw_fw) {
		g_debug("required section missing: firmware (type=FW_SECTION)");
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "required firmware section missing");
		return FALSE;
	}
	if (!saw_param) {
		g_debug("required section missing: parameter (type=PARAM)");
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "required parameter section missing");
		return FALSE;
	}
	if (!saw_fw_valid) {
		g_debug("required firmware section present but VALID bit is 0");
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "firmware section marked as invalid");
		return FALSE;
	}
	if (!saw_param_valid) {
		g_debug("required parameter section present but VALID bit is 0");
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "parameter section marked as invalid");
		return FALSE;
	}

	/* expose header values as standard metadata */
	g_autofree gchar *ver = g_strdup_printf("0x%04x", self->file_ver);
	fu_firmware_set_version(firmware, ver);

	/* your container size is the whole file */
	fu_firmware_set_size(firmware, sz);
	return TRUE;
}

static const char *
fu_pxi_tp_firmware_update_type_to_str(guint8 t)
{
	switch (t) {
	case PXI_TP_UPDATE_TYPE_GENERAL:
		return "GENERAL";
	case PXI_TP_UPDATE_TYPE_FW_SECTION:
		return "FW_SECTION";
	case PXI_TP_UPDATE_TYPE_BOOTLOADER:
		return "BOOTLOADER";
	case PXI_TP_UPDATE_TYPE_PARAM:
		return "PARAM";
	case PXI_TP_UPDATE_TYPE_TF_FORCE:
		return "TF_FORCE";
	default:
		return "UNKNOWN";
	}
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

/* insert both hex (kx) and decimal (kv ... _dec) */
static void
fu_pxi_tp_firmware_kx_and_dec(XbBuilderNode *bn, const char *key, guint64 v)
{
	fu_xmlb_builder_insert_kx(bn, key, v); /* hex */
	g_autofree gchar *kdec = g_strdup_printf("%s_dec", key);
	g_autofree gchar *vdec = g_strdup_printf("%" G_GUINT64_FORMAT, v);
	fu_xmlb_builder_insert_kv(bn, kdec, vdec);
}

static void
fu_pxi_tp_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuPxiTpFirmware *self = FU_PXI_TP_FIRMWARE(firmware);

	/* bytes for optional recompute / hexdumps */
	g_autoptr(GBytes) fw = fu_firmware_get_bytes_with_patches(firmware, NULL);
	const guint8 *d = fw ? g_bytes_get_data(fw, NULL) : NULL;
	gsize sz = fw ? g_bytes_get_size(fw) : 0;

	/* top-level identity and sizes */
	fu_xmlb_builder_insert_kv(bn, "magic", "FWHD");
	fu_pxi_tp_firmware_kx_and_dec(bn, "file_size", sz);
	fu_pxi_tp_firmware_kx_and_dec(bn, "header_length", self->header_len);
	/* payload size = total - header_len, bound to 0 */
	fu_pxi_tp_firmware_kx_and_dec(bn,
				      "payload_size",
				      (sz > self->header_len) ? (sz - self->header_len) : 0);

	/* header core fields */
	fu_pxi_tp_firmware_kx_and_dec(bn, "header_version", self->header_ver);
	fu_pxi_tp_firmware_kx_and_dec(bn, "file_version", self->file_ver);
	fu_pxi_tp_firmware_kx_and_dec(bn, "ic_part_id", self->ic_part_id);
	fu_pxi_tp_firmware_kx_and_dec(bn, "flash_sectors", self->flash_sectors);
	fu_pxi_tp_firmware_kx_and_dec(bn, "num_sections", self->num_sections);

	/* crcs (stored) */
	fu_pxi_tp_firmware_kx_and_dec(bn, "header_crc32", self->header_crc32);
	fu_pxi_tp_firmware_kx_and_dec(bn, "file_crc32", self->file_crc32);

	/* crcs (recomputed) + status */
	if (d != NULL && self->header_len >= 4 && self->header_len <= sz) {
		guint32 hdr_crc_calc = fu_crc32(FU_CRC_KIND_B32_STANDARD, d, self->header_len - 4);
		fu_pxi_tp_firmware_kx_and_dec(bn, "header_crc32_calc", hdr_crc_calc);
		fu_xmlb_builder_insert_kv(bn,
					  "header_crc32_ok",
					  (hdr_crc_calc == self->header_crc32) ? "true" : "false");
	}
	if (d != NULL && sz > self->header_len) {
		guint32 file_crc_calc =
		    fu_crc32(FU_CRC_KIND_B32_STANDARD, d + self->header_len, sz - self->header_len);
		fu_pxi_tp_firmware_kx_and_dec(bn, "file_crc32_calc", file_crc_calc);
		fu_xmlb_builder_insert_kv(bn,
					  "file_crc32_ok",
					  (file_crc_calc == self->file_crc32) ? "true" : "false");
	}

	/* ranges (handy for quick eyeballing) */
	if (self->header_len <= sz) {
		fu_pxi_tp_firmware_kx_and_dec(bn, "range_header_begin", 0);
		fu_pxi_tp_firmware_kx_and_dec(bn,
					      "range_header_end",
					      self->header_len); /* [0,header) */
		fu_pxi_tp_firmware_kx_and_dec(bn, "range_payload_begin", self->header_len);
		fu_pxi_tp_firmware_kx_and_dec(bn, "range_payload_end", sz);
	}

	/* sections */
	for (guint i = 0; self->sections && i < self->sections->len; i++) {
		FuPxiTpSection *s = g_ptr_array_index(self->sections, i);

		/* keys */
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
		/* new: reserved export */
		g_autofree gchar *p_reserved_hex = g_strdup_printf("section%u_reserved_hex", i);

		/* values */
		fu_pxi_tp_firmware_kx_and_dec(bn, p_type, s->update_type);
		fu_xmlb_builder_insert_kv(bn,
					  p_type_name,
					  fu_pxi_tp_firmware_update_type_to_str(s->update_type));
		fu_pxi_tp_firmware_kx_and_dec(bn, p_info, s->update_info);
		g_autofree gchar *flags_str =
		    fu_pxi_tp_firmware_update_info_to_flags(s->update_info);
		fu_xmlb_builder_insert_kv(bn, p_info_flags, flags_str);

		/* bools readable as true/false */
		fu_xmlb_builder_insert_kv(bn, p_is_valid, s->is_valid_update ? "true" : "false");
		fu_xmlb_builder_insert_kv(bn, p_is_ext, s->is_external ? "true" : "false");

		fu_pxi_tp_firmware_kx_and_dec(bn, p_flash_start, s->target_flash_start);
		fu_pxi_tp_firmware_kx_and_dec(bn, p_int_start, s->internal_file_start);
		fu_pxi_tp_firmware_kx_and_dec(bn, p_len, s->section_length);
		fu_pxi_tp_firmware_kx_and_dec(bn, p_crc, s->section_crc);

		/* new: dump reserved bytes as hex (all, no truncation) */
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
				/* recompute section CRC for diagnostics */
				guint32 calc = fu_crc32(FU_CRC_KIND_B32_STANDARD,
							d + s->internal_file_start,
							s->section_length);
				fu_pxi_tp_firmware_kx_and_dec(bn, p_crc_calc, calc);
				fu_xmlb_builder_insert_kv(bn,
							  p_crc_ok,
							  (calc == s->section_crc) ? "true"
										   : "false");

				g_autofree gchar *hex =
				    fu_pxi_tp_firmware_hexdump_slice(d + s->internal_file_start,
								     s->section_length,
								     32);
				fu_xmlb_builder_insert_kv(bn, p_sample_hex, hex);
			}
		} else {
			fu_xmlb_builder_insert_kv(bn, p_in_range, "n/a");
		}

		/* extname (already trimmed and NUL-terminated) */
		fu_xmlb_builder_insert_kv(bn, p_extname, s->external_file_name);
	}
}

/**
 * fu_pxi_tp_firmware_write:
 * @firmware: the container object
 * @error: return location for a #GError, or %NULL
 *
 * Serializes the container back to bytes. This parser is read-only for now,
 * so we simply return the original blob plus any applied patches.
 *
 * Returns: a new #GByteArray on success, or %NULL on error.
 */
static GByteArray *
fu_pxi_tp_firmware_write(FuFirmware *firmware, GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GBytes) fw = fu_firmware_get_bytes_with_patches(firmware, error);
	if (fw == NULL)
		return NULL;
	fu_byte_array_append_bytes(buf, fw);
	return g_steal_pointer(&buf);
}

/**
 * fu_pxi_tp_firmware_build:
 * @firmware: the container object
 * @n: parsed builder XML node (unused)
 * @error: return location for a #GError, or %NULL
 *
 * Optional builder hook for constructing a container from a .builder.xml.
 * Not used for the PixArt FWHD v1.0 parser; present to satisfy the vfunc.
 *
 * Returns: %TRUE always.
 */
static gboolean
fu_pxi_tp_firmware_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	(void)firmware;
	(void)n;
	(void)error; /* parser-only */
	return TRUE;
}

static void
fu_pxi_tp_firmware_init(FuPxiTpFirmware *self)
{
	self->sections = g_ptr_array_new_with_free_func(g_free);
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_CHECKSUM);
}

/* ---------------- finalize ---------------- */
static void
fu_pxi_tp_firmware_finalize(GObject *obj)
{
	FuPxiTpFirmware *self = FU_PXI_TP_FIRMWARE(obj);
	if (self->sections != NULL) {
		g_ptr_array_unref(self->sections); /* safe: has free_func=g_free */
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
}

/* ---------------- factory ----------------- */
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
fu_pxi_tp_firmware_get_file_version(FuPxiTpFirmware *self)
{
	g_return_val_if_fail(FU_IS_PXI_TP_FIRMWARE(self), 0);
	return self->file_ver;
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
	g_autoptr(GBytes) fw = fu_firmware_get_bytes_with_patches(FU_FIRMWARE(self), error);
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

	/* declare at the beginning of this small block to comply with C90 rules */
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
	g_autoptr(GBytes) fw = fu_firmware_get_bytes_with_patches(FU_FIRMWARE(self), error);
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
		guint64 sec_flash_begin;
		guint64 sec_flash_end;
		guint64 req_flash_begin;
		guint64 req_flash_end;
		FuPxiTpSection *s = g_ptr_array_index((GPtrArray *)secs, i);
		if (!s->is_valid_update || s->is_external)
			continue;

		sec_flash_begin = (guint64)s->target_flash_start;
		sec_flash_end = sec_flash_begin + (guint64)s->section_length; /* half-open */
		req_flash_begin = (guint64)flash_addr;
		req_flash_end = req_flash_begin + (guint64)len;

		if (req_flash_begin >= sec_flash_begin && req_flash_end <= sec_flash_end) {
			const guint64 off_in_sec = req_flash_begin - sec_flash_begin;
			const guint64 file_off_64 = (guint64)s->internal_file_start + off_in_sec;

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

			/* declare at the beginning of this small block to comply with C90 rules */
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
	    fu_pxi_tp_firmware_find_section_by_type(self, PXI_TP_UPDATE_TYPE_FW_SECTION);
	g_debug("file firmware CRC: 0x%08x", (guint)(s ? s->section_crc : 0));
	return s ? s->section_crc : 0;
}

guint32
fu_pxi_tp_firmware_get_file_parameter_crc(FuPxiTpFirmware *self)
{
	FuPxiTpSection *s = fu_pxi_tp_firmware_find_section_by_type(self, PXI_TP_UPDATE_TYPE_PARAM);
	g_debug("file parameter CRC: 0x%08x", (guint)(s ? s->section_crc : 0));
	return s ? s->section_crc : 0;
}

/* get the target_flash_start of the first section where update_type == 1 (FW_SECTION) and is
 * internal & valid */
guint32
fu_pxi_tp_firmware_get_firmware_address(FuPxiTpFirmware *self)
{
	FuPxiTpSection *s =
	    fu_pxi_tp_firmware_find_section_by_type(self, PXI_TP_UPDATE_TYPE_FW_SECTION);
	return s ? s->target_flash_start : 0;
}
