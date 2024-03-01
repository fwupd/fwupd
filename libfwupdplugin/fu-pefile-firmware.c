/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuFirmware"

#include "config.h"

#include "fu-byte-array.h"
#include "fu-bytes.h"
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

G_DEFINE_TYPE(FuPefileFirmware, fu_pefile_firmware, FU_TYPE_FIRMWARE)

#define FU_PEFILE_SECTION_ID_STRTAB_SIZE 16

static gboolean
fu_pefile_firmware_validate(FuFirmware *firmware,
			    GInputStream *stream,
			    gsize offset,
			    GError **error)
{
	return fu_struct_pe_dos_header_validate_stream(stream, offset, error);
}

static gboolean
fu_pefile_firmware_parse_section(FuFirmware *firmware,
				 GInputStream *stream,
				 gsize hdr_offset,
				 gsize strtab_offset,
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
		g_autofree gchar *str = NULL;

		if (!fu_strtoull(sect_id_tmp + 1, &str_idx, 0, G_MAXUINT32, error)) {
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
					    G_IO_ERROR,
					    G_IO_ERROR_INVALID_DATA,
					    "no section name");
			return FALSE;
		}
	} else {
		sect_id = g_steal_pointer(&sect_id_tmp);
	}

	/* create new firmware */
	if (g_strcmp0(sect_id, ".sbom") == 0) {
		img = fu_coswid_firmware_new();
	} else if (g_strcmp0(sect_id, ".sbat") == 0) {
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
						 fu_struct_pe_coff_section_get_virtual_size(st));
	if (!fu_firmware_parse_stream(img, img_stream, 0x0, flags, error)) {
		g_prefix_error(error, "failed to parse raw data %s: ", sect_id);
		return FALSE;
	}
	return fu_firmware_add_image_full(firmware, img, error);
}

static gboolean
fu_pefile_firmware_parse(FuFirmware *firmware,
			 GInputStream *stream,
			 gsize offset,
			 FwupdInstallFlags flags,
			 GError **error)
{
	gsize strtab_offset;
	guint32 nr_sections;
	g_autoptr(GByteArray) st_coff = NULL;
	g_autoptr(GByteArray) st_doshdr = NULL;

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

	/* verify optional extra header */
	if (fu_struct_pe_coff_file_header_get_size_of_optional_header(st_coff) > 0) {
		g_autoptr(GByteArray) st_opt =
		    fu_struct_pe_coff_optional_header64_parse_stream(stream, offset, error);
		if (st_opt == NULL) {
			g_prefix_error(error, "failed to read optional header: ");
			return FALSE;
		}
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
						      flags,
						      error)) {
			g_prefix_error(error, "failed to read section 0x%x: ", idx);
			return FALSE;
		}
		offset += FU_STRUCT_PE_COFF_SECTION_SIZE;
	}

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
fu_pefile_section_free(FuPefileSection *section)
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
	    g_ptr_array_new_with_free_func((GDestroyNotify)fu_pefile_section_free);

	/* calculate the offset for each of the sections */
	offset += st->len + st_hdr->len + st_opt->len;
	offset += FU_STRUCT_PE_COFF_SECTION_SIZE * imgs->len;
	for (guint i = 0; i < imgs->len; i++) {
		g_autofree FuPefileSection *section = g_new0(FuPefileSection, 1);
		FuFirmware *img = g_ptr_array_index(imgs, i);
		g_autoptr(GBytes) blob = NULL;

		section->offset = offset;
		section->blob = fu_firmware_write(img, error);
		if (section->blob == NULL)
			return NULL;
		section->id = g_strdup(fu_firmware_get_id(img));
		section->blobsz_aligned = fu_common_align_up(g_bytes_get_size(section->blob), 4);
		offset += section->blobsz_aligned;
		g_ptr_array_add(sections, g_steal_pointer(&section));
	}

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

		fu_struct_pe_coff_section_set_size_of_raw_data(st_sect, section->blobsz_aligned);
		fu_struct_pe_coff_section_set_virtual_address(st_sect, 0x0);
		fu_struct_pe_coff_section_set_virtual_size(st_sect,
							   g_bytes_get_size(section->blob));
		fu_struct_pe_coff_section_set_pointer_to_raw_data(st_sect, section->offset);

		/* set the name directly, or add to the string table */
		if (section->id == NULL) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
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
					    G_IO_ERROR,
					    G_IO_ERROR_INVALID_DATA,
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

static void
fu_pefile_firmware_init(FuPefileFirmware *self)
{
	fu_firmware_set_images_max(FU_FIRMWARE(self), 100);
}

static void
fu_pefile_firmware_class_init(FuPefileFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->validate = fu_pefile_firmware_validate;
	firmware_class->parse = fu_pefile_firmware_parse;
	firmware_class->write = fu_pefile_firmware_write;
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
