/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuFirmware"

#include "config.h"

#include "fu-bytes.h"
#include "fu-coswid-firmware.h"
#include "fu-csv-firmware.h"
#include "fu-mem.h"
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

static gboolean
fu_pefile_firmware_check_magic(FuFirmware *firmware, GBytes *fw, gsize offset, GError **error)
{
	return fu_struct_pe_dos_header_validate(g_bytes_get_data(fw, NULL),
						g_bytes_get_size(fw),
						offset,
						error);
}

static gboolean
fu_pefile_firmware_parse_section(FuFirmware *firmware,
				 GBytes *fw,
				 gsize hdr_offset,
				 gsize strtab_offset,
				 FwupdInstallFlags flags,
				 GError **error)
{
	gsize bufsz = 0;
	guint32 sect_offset;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);
	g_autofree gchar *sect_id = NULL;
	g_autofree gchar *sect_id_tmp = NULL;
	g_autoptr(FuFirmware) img = NULL;
	g_autoptr(GByteArray) st = NULL;
	g_autoptr(GBytes) blob = NULL;

	st = fu_struct_pe_coff_section_parse(buf, bufsz, hdr_offset, error);
	if (st == NULL)
		return FALSE;
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
		if (!fu_strtoull(sect_id_tmp + 1, &str_idx, 0, G_MAXUINT32, error))
			return FALSE;
		sect_id = fu_memstrsafe(buf, bufsz, strtab_offset + str_idx, 16, error);
		if (sect_id == NULL) {
			g_prefix_error(error, "no section name: ");
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
	blob = fu_bytes_new_offset(fw,
				   sect_offset,
				   fu_struct_pe_coff_section_get_size_of_raw_data(st),
				   error);
	if (blob == NULL) {
		g_prefix_error(error, "failed to get raw data for %s: ", sect_id);
		return FALSE;
	}
	if (!fu_firmware_parse(img, blob, flags, error)) {
		g_prefix_error(error, "failed to parse %s: ", sect_id);
		return FALSE;
	}
	return fu_firmware_add_image_full(firmware, img, error);
}

static gboolean
fu_pefile_firmware_parse(FuFirmware *firmware,
			 GBytes *fw,
			 gsize offset,
			 FwupdInstallFlags flags,
			 GError **error)
{
	gsize bufsz = 0;
	gsize strtab_offset;
	guint32 nr_sections;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);
	g_autoptr(GByteArray) st_coff = NULL;
	g_autoptr(GByteArray) st_doshdr = NULL;

	/* parse the DOS header to get the COFF header */
	st_doshdr = fu_struct_pe_dos_header_parse(buf, bufsz, offset, error);
	if (st_doshdr == NULL)
		return FALSE;
	offset += fu_struct_pe_dos_header_get_lfanew(st_doshdr);
	st_coff = fu_struct_pe_coff_file_header_parse(buf, bufsz, offset, error);
	if (st_coff == NULL)
		return FALSE;
	offset += st_coff->len;

	/* verify optional extra header */
	if (fu_struct_pe_coff_file_header_get_size_of_optional_header(st_coff) > 0) {
		g_autoptr(GByteArray) st_opt =
		    fu_struct_pe_coff_optional_header64_parse(buf, bufsz, offset, error);
		if (st_opt == NULL)
			return FALSE;
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
						      fw,
						      offset,
						      strtab_offset,
						      flags,
						      error))
			return FALSE;
		offset += FU_STRUCT_PE_COFF_SECTION_SIZE;
	}

	/* success */
	return TRUE;
}

static void
fu_pefile_firmware_init(FuPefileFirmware *self)
{
	fu_firmware_set_images_max(FU_FIRMWARE(self), 100);
}

static void
fu_pefile_firmware_class_init(FuPefileFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->check_magic = fu_pefile_firmware_check_magic;
	klass_firmware->parse = fu_pefile_firmware_parse;
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
