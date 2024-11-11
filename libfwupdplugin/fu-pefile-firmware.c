/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuFirmware"

#include "config.h"

#include "fu-byte-array.h"
#include "fu-bytes.h"
#include "fu-common.h"
#include "fu-composite-input-stream.h"
#include "fu-coswid-firmware.h"
#include "fu-csv-firmware.h"
#include "fu-input-stream.h"
#include "fu-mem.h"
#include "fu-partial-input-stream.h"
#include "fu-pefile-firmware.h"
#include "fu-pefile-struct.h"
#include "fu-sbatlevel-section.h"
#include "fu-string.h"

/**
 * FuPefileFirmware:
 *
 * A PE file consists of a Microsoft MS-DOS stub, the PE signature, the COFF file header, and an
 * optional header, followed by section data.
 *
 * Documented:
 * https://learn.microsoft.com/en-gb/windows/win32/debug/pe-format
 */

typedef struct {
	gchar *authenticode_hash;
	guint16 subsystem_id;
} FuPefileFirmwarePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuPefileFirmware, fu_pefile_firmware, FU_TYPE_FIRMWARE)
#define GET_PRIVATE(o) (fu_pefile_firmware_get_instance_private(o))

#define FU_PEFILE_SECTION_ID_STRTAB_SIZE 16

static void
fu_pefile_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuPefileFirmware *self = FU_PEFILE_FIRMWARE(firmware);
	FuPefileFirmwarePrivate *priv = GET_PRIVATE(self);
	fu_xmlb_builder_insert_kv(bn, "authenticode_hash", priv->authenticode_hash);
	fu_xmlb_builder_insert_kv(bn, "subsystem", fu_coff_subsystem_to_string(priv->subsystem_id));
}

static gboolean
fu_pefile_firmware_validate(FuFirmware *firmware,
			    GInputStream *stream,
			    gsize offset,
			    GError **error)
{
	return fu_struct_pe_dos_header_validate_stream(stream, offset, error);
}

typedef struct {
	gsize offset;
	gsize size;
	gchar *name;
} FuPefileFirmwareRegion;

static void
fu_pefile_firmware_add_region(GPtrArray *regions, const gchar *name, gsize offset, gsize size)
{
	FuPefileFirmwareRegion *r = g_new0(FuPefileFirmwareRegion, 1);
	r->name = g_strdup(name);
	r->offset = offset;
	r->size = size;
	g_ptr_array_add(regions, r);
}

static void
fu_pefile_firmware_region_free(FuPefileFirmwareRegion *r)
{
	g_free(r->name);
	g_free(r);
}

static gint
fu_pefile_firmware_region_sort_cb(gconstpointer a, gconstpointer b)
{
	const FuPefileFirmwareRegion *r1 = *((const FuPefileFirmwareRegion **)a);
	const FuPefileFirmwareRegion *r2 = *((const FuPefileFirmwareRegion **)b);
	if (r1->offset < r2->offset)
		return -1;
	if (r1->offset > r2->offset)
		return 1;
	return 0;
}

static gboolean
fu_pefile_firmware_parse_section(FuFirmware *firmware,
				 GInputStream *stream,
				 gsize hdr_offset,
				 gsize strtab_offset,
				 GPtrArray *regions,
				 FwupdInstallFlags flags,
				 GError **error)
{
	guint32 sect_offset;
	g_autofree gchar *sect_id = NULL;
	g_autofree gchar *sect_id_tmp = NULL;
	g_autoptr(FuFirmware) img = NULL;
	g_autoptr(GByteArray) st = NULL;
	g_autoptr(GInputStream) img_stream = NULL;

	st = fu_struct_pe_coff_section_parse_stream(stream, hdr_offset, error);
	if (st == NULL) {
		g_prefix_error(error, "failed to read section: ");
		return FALSE;
	}
	sect_id_tmp = fu_struct_pe_coff_section_get_name(st);
	if (sect_id_tmp == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "invalid section name");
		return FALSE;
	}
	if (sect_id_tmp[0] == '/') {
		guint64 str_idx = 0x0;
		guint8 buf[FU_PEFILE_SECTION_ID_STRTAB_SIZE] = {0};

		if (!fu_strtoull(sect_id_tmp + 1,
				 &str_idx,
				 0,
				 G_MAXUINT32,
				 FU_INTEGER_BASE_10,
				 error)) {
			g_prefix_error(error, "failed to parse section ID '%s': ", sect_id_tmp + 1);
			return FALSE;
		}
		if (!fu_input_stream_read_safe(stream,
					       buf,
					       sizeof(buf),
					       0x0,
					       strtab_offset + str_idx, /* seek */
					       sizeof(buf),
					       error))
			return FALSE;
		sect_id = fu_strsafe((const gchar *)buf, sizeof(buf));
		if (sect_id == NULL) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "no section name");
			return FALSE;
		}
	} else {
		sect_id = g_steal_pointer(&sect_id_tmp);
	}

	/* create new firmware */
	if (g_strcmp0(sect_id, ".sbom") == 0) {
		img = fu_coswid_firmware_new();
	} else if (g_strcmp0(sect_id, ".sbat") == 0 || g_strcmp0(sect_id, ".sbata") == 0 ||
		   g_strcmp0(sect_id, ".sbatl") == 0) {
		img = fu_csv_firmware_new();
		fu_csv_firmware_add_column_id(FU_CSV_FIRMWARE(img), "$id");
		fu_csv_firmware_add_column_id(FU_CSV_FIRMWARE(img), "$version_raw");
		fu_csv_firmware_add_column_id(FU_CSV_FIRMWARE(img), "vendor_name");
		fu_csv_firmware_add_column_id(FU_CSV_FIRMWARE(img), "vendor_package_name");
		fu_csv_firmware_add_column_id(FU_CSV_FIRMWARE(img), "$version");
		fu_csv_firmware_add_column_id(FU_CSV_FIRMWARE(img), "vendor_url");
	} else if (g_strcmp0(sect_id, ".sbatlevel") == 0) {
		img = fu_sbatlevel_section_new();
	} else {
		img = fu_firmware_new();
	}
	fu_firmware_set_id(img, sect_id);

	/* add data */
	sect_offset = fu_struct_pe_coff_section_get_pointer_to_raw_data(st);
	fu_firmware_set_offset(img, sect_offset);
	img_stream = fu_partial_input_stream_new(stream,
						 sect_offset,
						 fu_struct_pe_coff_section_get_size_of_raw_data(st),
						 error);
	if (img_stream == NULL)
		return FALSE;
	if (!fu_firmware_parse_stream(img, img_stream, 0x0, flags, error)) {
		g_prefix_error(error, "failed to parse raw data %s: ", sect_id);
		return FALSE;
	}

	/* add region for Authenticode checksum */
	fu_pefile_firmware_add_region(regions,
				      sect_id,
				      sect_offset,
				      fu_struct_pe_coff_section_get_size_of_raw_data(st));

	/* success */
	return fu_firmware_add_image_full(firmware, img, error);
}

static gboolean
fu_pefile_firmware_parse(FuFirmware *firmware,
			 GInputStream *stream,
			 FwupdInstallFlags flags,
			 GError **error)
{
	FuPefileFirmware *self = FU_PEFILE_FIRMWARE(firmware);
	FuPefileFirmwarePrivate *priv = GET_PRIVATE(self);
	guint32 cert_table_sz = 0;
	gsize offset = 0;
	gsize streamsz = 0;
	gsize strtab_offset;
	guint32 nr_sections;
	g_autoptr(FuStructPeCoffFileHeader) st_coff = NULL;
	g_autoptr(FuStructPeDosHeader) st_doshdr = NULL;
	g_autoptr(GPtrArray) regions = NULL;
	g_autoptr(GInputStream) composite_stream = fu_composite_input_stream_new();

	/* get size */
	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;

	/* parse the DOS header to get the COFF header */
	st_doshdr = fu_struct_pe_dos_header_parse_stream(stream, offset, error);
	if (st_doshdr == NULL) {
		g_prefix_error(error, "failed to read DOS header: ");
		return FALSE;
	}
	offset += fu_struct_pe_dos_header_get_lfanew(st_doshdr);
	st_coff = fu_struct_pe_coff_file_header_parse_stream(stream, offset, error);
	if (st_coff == NULL) {
		g_prefix_error(error, "failed to read COFF header: ");
		return FALSE;
	}
	offset += st_coff->len;

	regions = g_ptr_array_new_with_free_func((GDestroyNotify)fu_pefile_firmware_region_free);

	/* 1st Authenticode region */
	fu_pefile_firmware_add_region(regions,
				      "pre-cksum",
				      0x0,
				      offset + FU_STRUCT_PE_COFF_OPTIONAL_HEADER64_OFFSET_CHECKSUM);

	if (!fu_input_stream_read_safe(
		stream,
		(guint8 *)&priv->subsystem_id,
		sizeof(priv->subsystem_id),
		0x0,
		offset + FU_STRUCT_PE_COFF_OPTIONAL_HEADER64_OFFSET_SUBSYSTEM, /* seek */
		sizeof(priv->subsystem_id),
		error))
		return FALSE;

	/* 2nd Authenticode region */
	fu_pefile_firmware_add_region(
	    regions,
	    "chksum->cert-table",
	    offset + FU_STRUCT_PE_COFF_OPTIONAL_HEADER64_OFFSET_SUBSYSTEM,
	    FU_STRUCT_PE_COFF_OPTIONAL_HEADER64_OFFSET_CERTIFICATE_TABLE -
		FU_STRUCT_PE_COFF_OPTIONAL_HEADER64_OFFSET_SUBSYSTEM); // end

	/* verify optional extra header */
	if (fu_struct_pe_coff_file_header_get_size_of_optional_header(st_coff) > 0) {
		g_autoptr(FuStructPeCoffOptionalHeader64) st_opt =
		    fu_struct_pe_coff_optional_header64_parse_stream(stream, offset, error);
		if (st_opt == NULL) {
			g_prefix_error(error, "failed to read optional header: ");
			return FALSE;
		}

		/* 3rd Authenticode region */
		if (fu_struct_pe_coff_optional_header64_get_size_of_headers(st_opt) > 0) {
			fu_pefile_firmware_add_region(
			    regions,
			    "cert-table->end-of-headers",
			    offset + FU_STRUCT_PE_COFF_OPTIONAL_HEADER64_OFFSET_DEBUG_TABLE,
			    fu_struct_pe_coff_optional_header64_get_size_of_headers(st_opt) -
				(offset + FU_STRUCT_PE_COFF_OPTIONAL_HEADER64_OFFSET_DEBUG_TABLE));
		}

		/* 4th Authenticode region */
		cert_table_sz =
		    fu_struct_pe_coff_optional_header64_get_size_of_certificate_table(st_opt);

		offset += fu_struct_pe_coff_file_header_get_size_of_optional_header(st_coff);
	}

	/* read number of sections */
	nr_sections = fu_struct_pe_coff_file_header_get_number_of_sections(st_coff);
	if (nr_sections == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "invalid number of sections");
		return FALSE;
	}
	strtab_offset = fu_struct_pe_coff_file_header_get_pointer_to_symbol_table(st_coff) +
			fu_struct_pe_coff_file_header_get_number_of_symbols(st_coff) *
			    FU_STRUCT_PE_COFF_SYMBOL_SIZE;

	/* read out each section */
	for (guint idx = 0; idx < nr_sections; idx++) {
		if (!fu_pefile_firmware_parse_section(firmware,
						      stream,
						      offset,
						      strtab_offset,
						      regions,
						      flags,
						      error)) {
			g_prefix_error(error, "failed to read section 0x%x: ", idx);
			return FALSE;
		}
		offset += FU_STRUCT_PE_COFF_SECTION_SIZE;
	}

	/* make sure ordered by address */
	g_ptr_array_sort(regions, fu_pefile_firmware_region_sort_cb);

	/* for the data at the end of the image */
	if (regions->len > 0) {
		FuPefileFirmwareRegion *r = g_ptr_array_index(regions, regions->len - 1);
		gsize offset_end = r->offset + r->size;
		fu_pefile_firmware_add_region(regions,
					      "tabledata->cert-table",
					      offset_end,
					      streamsz - (offset_end + cert_table_sz));
	}

	/* calculate the checksum we would find in the dbx */
	for (guint i = 0; i < regions->len; i++) {
		FuPefileFirmwareRegion *r = g_ptr_array_index(regions, i);
		g_autoptr(GInputStream) partial_stream = NULL;

		if (r->size == 0)
			continue;
		g_debug("Authenticode region %s: 0x%04x -> 0x%04x [0x%04x]",
			r->name,
			(guint)r->offset,
			(guint)(r->offset + r->size),
			(guint)r->size);
		partial_stream = fu_partial_input_stream_new(stream, r->offset, r->size, error);
		if (partial_stream == NULL)
			return FALSE;
		fu_composite_input_stream_add_partial_stream(
		    FU_COMPOSITE_INPUT_STREAM(composite_stream),
		    FU_PARTIAL_INPUT_STREAM(partial_stream));
	}
	priv->authenticode_hash =
	    fu_input_stream_compute_checksum(composite_stream, G_CHECKSUM_SHA256, error);
	if (priv->authenticode_hash == NULL)
		return FALSE;

	/* success */
	return TRUE;
}

typedef struct {
	GBytes *blob;
	gchar *id;
	gsize offset;
	gsize blobsz_aligned;
} FuPefileSection;

static void
fu_pefile_firmware_section_free(FuPefileSection *section)
{
	if (section->blob != NULL)
		g_bytes_unref(section->blob);
	g_free(section->id);
	g_free(section);
}

static GByteArray *
fu_pefile_firmware_write(FuFirmware *firmware, GError **error)
{
	gsize offset = 0;
	g_autoptr(GPtrArray) imgs = fu_firmware_get_images(firmware);
	g_autoptr(GByteArray) st = fu_struct_pe_dos_header_new();
	g_autoptr(GByteArray) st_hdr = fu_struct_pe_coff_file_header_new();
	g_autoptr(GByteArray) st_opt = fu_struct_pe_coff_optional_header64_new();
	g_autoptr(GByteArray) strtab = g_byte_array_new();
	g_autoptr(GPtrArray) sections =
	    g_ptr_array_new_with_free_func((GDestroyNotify)fu_pefile_firmware_section_free);

	/* calculate the offset for each of the sections */
	offset += st->len + st_hdr->len + st_opt->len;
	offset += FU_STRUCT_PE_COFF_SECTION_SIZE * imgs->len;
	for (guint i = 0; i < imgs->len; i++) {
		g_autofree FuPefileSection *section = g_new0(FuPefileSection, 1);
		FuFirmware *img = g_ptr_array_index(imgs, i);

		section->offset = offset;
		section->blob = fu_firmware_write(img, error);
		if (section->blob == NULL)
			return NULL;
		section->id = g_strdup(fu_firmware_get_id(img));
		section->blobsz_aligned = fu_common_align_up(g_bytes_get_size(section->blob), 4);
		offset += section->blobsz_aligned;
		g_ptr_array_add(sections, g_steal_pointer(&section));
	}

	/* export_table -> architecture_table */
	fu_struct_pe_coff_optional_header64_set_number_of_rva_and_sizes(st_opt, 7);

	/* COFF file header */
	fu_struct_pe_coff_file_header_set_size_of_optional_header(st_hdr, st_opt->len);
	fu_struct_pe_coff_file_header_set_number_of_sections(st_hdr, sections->len);
	fu_struct_pe_coff_file_header_set_pointer_to_symbol_table(st_hdr, offset);
	g_byte_array_append(st, st_hdr->data, st_hdr->len);
	g_byte_array_append(st, st_opt->data, st_opt->len);

	/* add sections */
	for (guint i = 0; i < sections->len; i++) {
		FuPefileSection *section = g_ptr_array_index(sections, i);
		g_autoptr(GByteArray) st_sect = fu_struct_pe_coff_section_new();

		fu_struct_pe_coff_section_set_size_of_raw_data(st_sect,
							       g_bytes_get_size(section->blob));
		fu_struct_pe_coff_section_set_virtual_address(st_sect, 0x0);
		fu_struct_pe_coff_section_set_virtual_size(st_sect, section->blobsz_aligned);
		fu_struct_pe_coff_section_set_pointer_to_raw_data(st_sect, section->offset);

		/* set the name directly, or add to the string table */
		if (section->id == NULL) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "image %u has no ID",
				    i);
			return NULL;
		}
		if (strlen(section->id) <= 8) {
			if (!fu_struct_pe_coff_section_set_name(st_sect, section->id, error))
				return NULL;
		} else {
			g_autofree gchar *name_tmp = g_strdup_printf("/%u", strtab->len);
			g_autoptr(GByteArray) strtab_buf = g_byte_array_new();

			if (!fu_struct_pe_coff_section_set_name(st_sect, name_tmp, error))
				return NULL;

			/* create a byte buffer of exactly the correct chunk size */
			g_byte_array_append(strtab_buf,
					    (const guint8 *)section->id,
					    strlen(section->id));
			if (strtab_buf->len > FU_PEFILE_SECTION_ID_STRTAB_SIZE) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "image ID %s is too long",
					    section->id);
				return NULL;
			}
			fu_byte_array_set_size(strtab_buf, FU_PEFILE_SECTION_ID_STRTAB_SIZE, 0x0);
			g_byte_array_append(strtab, strtab_buf->data, strtab_buf->len);
		}
		g_byte_array_append(st, st_sect->data, st_sect->len);
	}

	/* add the section data itself */
	for (guint i = 0; i < sections->len; i++) {
		FuPefileSection *section = g_ptr_array_index(sections, i);
		g_autoptr(GBytes) blob_aligned =
		    fu_bytes_pad(section->blob, section->blobsz_aligned);
		fu_byte_array_append_bytes(st, blob_aligned);
	}

	/* string table comes last */
	g_byte_array_append(st, strtab->data, strtab->len);

	/* success */
	return g_steal_pointer(&st);
}

static gchar *
fu_pefile_firmware_get_checksum(FuFirmware *firmware, GChecksumType csum_kind, GError **error)
{
	FuPefileFirmware *self = FU_PEFILE_FIRMWARE(firmware);
	FuPefileFirmwarePrivate *priv = GET_PRIVATE(self);
	if (csum_kind != G_CHECKSUM_SHA256) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "Authenticode only supports SHA256");
		return NULL;
	}
	if (priv->authenticode_hash == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "Authenticode checksum not set");
		return NULL;
	}
	return g_strdup(priv->authenticode_hash);
}

static void
fu_pefile_firmware_init(FuPefileFirmware *self)
{
	fu_firmware_set_images_max(FU_FIRMWARE(self), 100);
}

static void
fu_pefile_firmware_finalize(GObject *object)
{
	FuPefileFirmware *self = FU_PEFILE_FIRMWARE(object);
	FuPefileFirmwarePrivate *priv = GET_PRIVATE(self);
	g_free(priv->authenticode_hash);
	G_OBJECT_CLASS(fu_pefile_firmware_parent_class)->finalize(object);
}

static void
fu_pefile_firmware_class_init(FuPefileFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_pefile_firmware_finalize;
	firmware_class->validate = fu_pefile_firmware_validate;
	firmware_class->parse = fu_pefile_firmware_parse;
	firmware_class->write = fu_pefile_firmware_write;
	firmware_class->export = fu_pefile_firmware_export;
	firmware_class->get_checksum = fu_pefile_firmware_get_checksum;
}

/**
 * fu_pefile_firmware_new:
 *
 * Creates a new #FuPefileFirmware
 *
 * Since: 1.8.10
 **/
FuFirmware *
fu_pefile_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_PEFILE_FIRMWARE, NULL));
}
