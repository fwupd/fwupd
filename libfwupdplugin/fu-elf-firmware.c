/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuFirmware"

#include "config.h"

#include "fu-bytes.h"
#include "fu-elf-firmware.h"
#include "fu-elf-struct.h"
#include "fu-input-stream.h"
#include "fu-partial-input-stream.h"
#include "fu-string.h"

/**
 * FuElfFirmware:
 *
 * Executable and Linkable Format is a common standard file format for executable files,
 * object code, shared libraries, core dumps -- and sometimes firmware.
 *
 * Documented:
 * https://en.wikipedia.org/wiki/Executable_and_Linkable_Format
 */

G_DEFINE_TYPE(FuElfFirmware, fu_elf_firmware, FU_TYPE_FIRMWARE)

static gboolean
fu_elf_firmware_validate(FuFirmware *firmware, GInputStream *stream, gsize offset, GError **error)
{
	return fu_struct_elf_file_header64le_validate_stream(stream, offset, error);
}

static gboolean
fu_elf_firmware_parse(FuFirmware *firmware,
		      GInputStream *stream,
		      gsize offset,
		      FwupdInstallFlags flags,
		      GError **error)
{
	gsize offset_secthdr = offset;
	gsize offset_proghdr = offset;
	guint16 phentsize;
	guint16 phnum;
	guint16 shnum;
	g_autoptr(GByteArray) st_fhdr = NULL;
	g_autoptr(GByteArray) shstrndx_buf = NULL;
	g_autoptr(GPtrArray) sections =
	    g_ptr_array_new_with_free_func((GDestroyNotify)g_byte_array_unref);

	/* file header */
	st_fhdr = fu_struct_elf_file_header64le_parse_stream(stream, offset, error);
	if (st_fhdr == NULL)
		return FALSE;

	/* parse each program header, unused here */
	offset_proghdr += fu_struct_elf_file_header64le_get_phoff(st_fhdr);
	phentsize = fu_struct_elf_file_header64le_get_phentsize(st_fhdr);
	phnum = fu_struct_elf_file_header64le_get_phnum(st_fhdr);
	for (guint i = 0; i < phnum; i++) {
		g_autoptr(GByteArray) st_phdr =
		    fu_struct_elf_program_header64le_parse_stream(stream, offset_proghdr, error);
		if (st_phdr == NULL)
			return FALSE;
		offset_proghdr += phentsize;
	}

	/* parse all the sections ahead of time */
	offset_secthdr += fu_struct_elf_file_header64le_get_shoff(st_fhdr);
	shnum = fu_struct_elf_file_header64le_get_shnum(st_fhdr);
	for (guint i = 0; i < shnum; i++) {
		g_autoptr(GByteArray) st_shdr =
		    fu_struct_elf_section_header64le_parse_stream(stream, offset_secthdr, error);
		if (st_shdr == NULL)
			return FALSE;
		g_ptr_array_add(sections, g_steal_pointer(&st_shdr));
		offset_secthdr += fu_struct_elf_file_header64le_get_shentsize(st_fhdr);
	}

	/* add sections as images */
	for (guint i = 0; i < sections->len; i++) {
		GByteArray *st_shdr = g_ptr_array_index(sections, i);
		guint64 sect_offset = fu_struct_elf_section_header64le_get_offset(st_shdr);
		guint64 sect_size = fu_struct_elf_section_header64le_get_size(st_shdr);
		g_autoptr(FuFirmware) img = fu_firmware_new();

		/* catch the strtab */
		if (i == fu_struct_elf_file_header64le_get_shstrndx(st_fhdr)) {
			if (fu_struct_elf_section_header64le_get_type(st_shdr) !=
			    FU_ELF_SECTION_HEADER_TYPE_STRTAB) {
				g_set_error(
				    error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "shstrndx section type was not strtab, was %s",
				    fu_elf_section_header_type_to_string(
					fu_struct_elf_section_header64le_get_type(shstrndx_buf)));
				return FALSE;
			}
			shstrndx_buf = fu_input_stream_read_byte_array(stream,
								       offset + sect_offset,
								       sect_size,
								       error);
			if (shstrndx_buf == NULL)
				return FALSE;
			continue;
		}

		if (fu_struct_elf_section_header64le_get_type(st_shdr) ==
			FU_ELF_SECTION_HEADER_TYPE_NULL ||
		    fu_struct_elf_section_header64le_get_type(st_shdr) ==
			FU_ELF_SECTION_HEADER_TYPE_STRTAB)
			continue;
		if (sect_size > 0) {
			g_autoptr(GInputStream) img_stream =
			    fu_partial_input_stream_new(stream,
							offset + sect_offset,
							sect_size,
							error);
			if (img_stream == NULL)
				return FALSE;
			if (!fu_firmware_parse_stream(img, img_stream, 0x0, flags, error))
				return FALSE;
		}
		fu_firmware_set_idx(img, i);
		if (!fu_firmware_add_image_full(firmware, img, error))
			return FALSE;
	}

	/* no shstrndx found */
	if (shstrndx_buf == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "shstrndx was invalid");
		return FALSE;
	}

	/* fix up the section names */
	for (guint i = 0; i < sections->len; i++) {
		GByteArray *st_shdr = g_ptr_array_index(sections, i);
		guint32 sh_name = fu_struct_elf_section_header64le_get_name(st_shdr);
		g_autofree gchar *name = NULL;
		g_autoptr(FuFirmware) img = NULL;

		if (fu_struct_elf_section_header64le_get_type(st_shdr) ==
			FU_ELF_SECTION_HEADER_TYPE_NULL ||
		    fu_struct_elf_section_header64le_get_type(st_shdr) ==
			FU_ELF_SECTION_HEADER_TYPE_STRTAB)
			continue;
		if (sh_name > shstrndx_buf->len) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "offset into shstrndx invalid for section 0x%x",
				    i);
			return FALSE;
		}
		img = fu_firmware_get_image_by_idx(firmware, i, error);
		if (img == NULL)
			return FALSE;
		name = g_strndup((const gchar *)shstrndx_buf->data + sh_name,
				 shstrndx_buf->len - sh_name);
		if (name != NULL && name[0] != '\0')
			fu_firmware_set_id(img, name);
	}

	/* success */
	return TRUE;
}

static void
fu_elf_firmware_init(FuElfFirmware *self)
{
	fu_firmware_set_images_max(FU_FIRMWARE(self), 1024);
}

static void
fu_elf_firmware_class_init(FuElfFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->validate = fu_elf_firmware_validate;
	firmware_class->parse = fu_elf_firmware_parse;
}

/**
 * fu_elf_firmware_new:
 *
 * Creates a new #FuElfFirmware
 *
 * Since: 1.9.3
 **/
FuFirmware *
fu_elf_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_ELF_FIRMWARE, NULL));
}
