/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuFirmware"

#include "config.h"

#include "fu-byte-array.h"
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
		      FuFirmwareParseFlags flags,
		      GError **error)
{
	gsize offset_secthdr = 0;
	gsize offset_proghdr = 0;
	guint16 phentsize;
	guint16 phnum;
	guint16 shnum;
	g_autoptr(GByteArray) st_fhdr = NULL;
	g_autoptr(GByteArray) shstrndx_buf = NULL;
	g_autoptr(GPtrArray) sections =
	    g_ptr_array_new_with_free_func((GDestroyNotify)g_byte_array_unref);

	/* file header */
	st_fhdr = fu_struct_elf_file_header64le_parse_stream(stream, 0x0, error);
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
		g_autoptr(FuStructElfSectionHeader64le) st_shdr =
		    fu_struct_elf_section_header64le_parse_stream(stream, offset_secthdr, error);
		if (st_shdr == NULL)
			return FALSE;
		g_ptr_array_add(sections, g_steal_pointer(&st_shdr));
		offset_secthdr += fu_struct_elf_file_header64le_get_shentsize(st_fhdr);
	}

	/* add sections as images */
	for (guint i = 0; i < sections->len; i++) {
		FuStructElfSectionHeader64le *st_shdr = g_ptr_array_index(sections, i);
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
					fu_struct_elf_section_header64le_get_type(st_shdr)));
				return FALSE;
			}
			shstrndx_buf = fu_input_stream_read_byte_array(stream,
								       sect_offset,
								       sect_size,
								       NULL,
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
			    fu_partial_input_stream_new(stream, sect_offset, sect_size, error);
			if (img_stream == NULL) {
				g_prefix_error_literal(error, "failed to cut EFI image: ");
				return FALSE;
			}
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
		FuStructElfSectionHeader64le *st_shdr = g_ptr_array_index(sections, i);
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

typedef struct {
	gchar *name;
	gsize namesz;
	gsize offset;
} FuElfFirmwareStrtabEntry;

static void
fu_elf_firmware_strtab_entry_free(FuElfFirmwareStrtabEntry *entry)
{
	g_free(entry->name);
	g_free(entry);
}

static void
fu_elf_firmware_strtab_insert(GPtrArray *strtab, const gchar *name)
{
	FuElfFirmwareStrtabEntry *entry = g_new0(FuElfFirmwareStrtabEntry, 1);
	gsize offset = 0;

	g_return_if_fail(name != NULL);

	/* get the previous entry */
	if (strtab->len > 0) {
		FuElfFirmwareStrtabEntry *entry_old = g_ptr_array_index(strtab, strtab->len - 1);
		offset += entry_old->offset + entry_old->namesz;
	}
	entry->namesz = strlen(name) + 1; /* with NUL */
	entry->name = g_strdup(name);
	entry->offset = offset;
	g_ptr_array_add(strtab, entry);
}

static GPtrArray *
fu_elf_firmware_strtab_new(void)
{
	g_autoptr(GPtrArray) strtab =
	    g_ptr_array_new_with_free_func((GDestroyNotify)fu_elf_firmware_strtab_entry_free);
	fu_elf_firmware_strtab_insert(strtab, "");
	fu_elf_firmware_strtab_insert(strtab, ".shstrtab");
	return g_steal_pointer(&strtab);
}

static GByteArray *
fu_elf_firmware_strtab_write(GPtrArray *strtab)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();
	for (guint i = 0; i < strtab->len; i++) {
		FuElfFirmwareStrtabEntry *entry = g_ptr_array_index(strtab, i);
		g_byte_array_append(buf, (const guint8 *)entry->name, entry->namesz);
	}
	return g_steal_pointer(&buf);
}

static gsize
fu_elf_firmware_strtab_get_offset_for_name(GPtrArray *strtab, const gchar *name)
{
	for (guint i = 0; i < strtab->len; i++) {
		FuElfFirmwareStrtabEntry *entry = g_ptr_array_index(strtab, i);
		if (g_strcmp0(entry->name, name) == 0)
			return entry->offset;
	}
	return 0;
}

static GByteArray *
fu_elf_firmware_write(FuFirmware *firmware, GError **error)
{
	const gsize physical_addr = 0x80000000;
	gsize section_offset = 0;
	g_autoptr(FuStructElfFileHeader64le) st_filehdr = fu_struct_elf_file_header64le_new();
	g_autoptr(FuStructElfProgramHeader64le) st_proghdr = fu_struct_elf_program_header64le_new();
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GByteArray) section_data = g_byte_array_new();
	g_autoptr(GByteArray) section_hdr = g_byte_array_new();
	g_autoptr(GByteArray) shstrtab = NULL;
	g_autoptr(GPtrArray) imgs = NULL;
	g_autoptr(GPtrArray) strtab = fu_elf_firmware_strtab_new();

	/* build the string table:
	 *
	 *    \0
	 *    .text\0
	 *    .rodata\0
	 */
	imgs = fu_firmware_get_images(firmware);
	for (guint i = 0; i < imgs->len; i++) {
		FuFirmware *img = g_ptr_array_index(imgs, i);
		if (fu_firmware_get_id(img) == NULL) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "section 0x%x must have an ID",
				    (guint)fu_firmware_get_idx(img));
			return NULL;
		}
		fu_elf_firmware_strtab_insert(strtab, fu_firmware_get_id(img));
	}
	shstrtab = fu_elf_firmware_strtab_write(strtab);

	/* build the section data:
	 *
	 *    shstrtab
	 *    [img]
	 *    [img]
	 *    [img]
	 *
	 * NOTE: requires shstrtab to be set
	 */
	g_byte_array_append(section_data, shstrtab->data, shstrtab->len);
	for (guint i = 0; i < imgs->len; i++) {
		FuFirmware *img = g_ptr_array_index(imgs, i);
		g_autoptr(GBytes) blob = fu_firmware_get_bytes(img, error);
		if (blob == NULL)
			return NULL;
		fu_byte_array_append_bytes(section_data, blob);
	}

	/* calculate the offset of each section */
	section_offset = st_filehdr->len + st_proghdr->len + shstrtab->len;
	for (guint i = 0; i < imgs->len; i++) {
		FuFirmware *img = g_ptr_array_index(imgs, i);
		fu_firmware_set_offset(img, section_offset);
		section_offset += fu_firmware_get_size(img);
	}

	/* build the section header:
	 *  1. empty section header
	 *  2. [image] section headers
	 *  3. shstrtab
	 *
	 * NOTE: requires image offset to be set
	 */
	if (imgs->len > 0) {
		g_autoptr(FuStructElfSectionHeader64le) st_secthdr =
		    fu_struct_elf_section_header64le_new();
		g_byte_array_append(section_hdr, st_secthdr->data, st_secthdr->len);
	}
	for (guint i = 0; i < imgs->len; i++) {
		FuFirmware *img = g_ptr_array_index(imgs, i);
		g_autoptr(FuStructElfSectionHeader64le) st_secthdr =
		    fu_struct_elf_section_header64le_new();
		gsize strtab_offset =
		    fu_elf_firmware_strtab_get_offset_for_name(strtab, fu_firmware_get_id(img));
		fu_struct_elf_section_header64le_set_name(st_secthdr, strtab_offset);
		fu_struct_elf_section_header64le_set_type(st_secthdr,
							  FU_ELF_SECTION_HEADER_TYPE_PROGBITS);
		fu_struct_elf_section_header64le_set_flags(st_secthdr, 0x02);
		fu_struct_elf_section_header64le_set_addr(st_secthdr,
							  physical_addr +
							      fu_firmware_get_offset(img));
		fu_struct_elf_section_header64le_set_offset(st_secthdr,
							    fu_firmware_get_offset(img));
		fu_struct_elf_section_header64le_set_size(st_secthdr, fu_firmware_get_size(img));
		g_byte_array_append(section_hdr, st_secthdr->data, st_secthdr->len);
	}
	if (shstrtab->len > 0) {
		g_autoptr(FuStructElfSectionHeader64le) st_secthdr =
		    fu_struct_elf_section_header64le_new();
		fu_struct_elf_section_header64le_set_name(st_secthdr,
							  0x1); /* we made sure this was first */
		fu_struct_elf_section_header64le_set_type(st_secthdr,
							  FU_ELF_SECTION_HEADER_TYPE_STRTAB);
		fu_struct_elf_section_header64le_set_offset(st_secthdr,
							    st_filehdr->len + st_proghdr->len);
		fu_struct_elf_section_header64le_set_size(st_secthdr, shstrtab->len);
		g_byte_array_append(section_hdr, st_secthdr->data, st_secthdr->len);
	}

	/* update with the new totals */
	fu_struct_elf_file_header64le_set_entry(st_filehdr, physical_addr + 0x60);
	fu_struct_elf_file_header64le_set_shoff(st_filehdr,
						st_filehdr->len + st_proghdr->len +
						    section_data->len);
	fu_struct_elf_file_header64le_set_phentsize(st_filehdr,
						    FU_STRUCT_ELF_PROGRAM_HEADER64LE_SIZE);
	fu_struct_elf_file_header64le_set_phnum(st_filehdr, 1);
	fu_struct_elf_file_header64le_set_shentsize(st_filehdr,
						    FU_STRUCT_ELF_SECTION_HEADER64LE_SIZE);
	fu_struct_elf_file_header64le_set_shnum(st_filehdr, 2 + imgs->len); /* <null> & shstrtab */
	fu_struct_elf_file_header64le_set_shstrndx(st_filehdr, imgs->len + 1);
	fu_struct_elf_program_header64le_set_vaddr(st_proghdr, physical_addr);
	fu_struct_elf_program_header64le_set_paddr(st_proghdr, physical_addr);
	fu_struct_elf_program_header64le_set_filesz(st_proghdr,
						    st_filehdr->len + st_proghdr->len +
							section_data->len + section_hdr->len);
	fu_struct_elf_program_header64le_set_memsz(st_proghdr,
						   st_filehdr->len + st_proghdr->len +
						       section_data->len + section_hdr->len);

	/* add file header, sections, then section headers */
	g_byte_array_append(buf, st_filehdr->data, st_filehdr->len);
	g_byte_array_append(buf, st_proghdr->data, st_proghdr->len);
	g_byte_array_append(buf, section_data->data, section_data->len);
	g_byte_array_append(buf, section_hdr->data, section_hdr->len);
	return g_steal_pointer(&buf);
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
	firmware_class->write = fu_elf_firmware_write;
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
