/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuFirmware"

#include "config.h"

#include "fu-bytes.h"
#include "fu-elf-firmware.h"
#include "fu-elf-struct.h"
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
fu_elf_firmware_check_magic(FuFirmware *firmware, GBytes *fw, gsize offset, GError **error)
{
	return fu_struct_elf_file_header64le_validate(g_bytes_get_data(fw, NULL),
						      g_bytes_get_size(fw),
						      offset,
						      error);
}

static gboolean
fu_elf_firmware_parse(FuFirmware *firmware,
		      GBytes *fw,
		      gsize offset,
		      FwupdInstallFlags flags,
		      GError **error)
{
	gsize bufsz = 0;
	gsize offset_secthdr = offset;
	gsize offset_proghdr = offset;
	guint16 phentsize;
	guint16 phnum;
	guint16 shnum;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);
	g_autoptr(GByteArray) st_fhdr = NULL;
	g_autoptr(GBytes) shstrndx_blob = NULL;
	g_autoptr(GPtrArray) sections =
	    g_ptr_array_new_with_free_func((GDestroyNotify)g_byte_array_unref);

	/* file header */
	st_fhdr = fu_struct_elf_file_header64le_parse(buf, bufsz, offset, error);
	if (st_fhdr == NULL)
		return FALSE;

	/* parse each program header, unused here */
	offset_proghdr += fu_struct_elf_file_header64le_get_phoff(st_fhdr);
	phentsize = fu_struct_elf_file_header64le_get_phentsize(st_fhdr);
	phnum = fu_struct_elf_file_header64le_get_phnum(st_fhdr);
	for (guint i = 0; i < phnum; i++) {
		g_autoptr(GByteArray) st_phdr =
		    fu_struct_elf_program_header64le_parse(buf, bufsz, offset_proghdr, error);
		if (st_phdr == NULL)
			return FALSE;
		offset_proghdr += phentsize;
	}

	/* parse all the sections ahead of time */
	offset_secthdr += fu_struct_elf_file_header64le_get_shoff(st_fhdr);
	shnum = fu_struct_elf_file_header64le_get_shnum(st_fhdr);
	for (guint i = 0; i < shnum; i++) {
		g_autoptr(GByteArray) st_shdr =
		    fu_struct_elf_section_header64le_parse(buf, bufsz, offset_secthdr, error);
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
		if (sect_size > 0) {
			g_autoptr(GBytes) blob =
			    fu_bytes_new_offset(fw, offset + sect_offset, sect_size, error);
			if (blob == NULL)
				return FALSE;
			fu_firmware_set_bytes(img, blob);
		}
		fu_firmware_set_idx(img, i);
		fu_firmware_add_image(firmware, img);
	}

	/* fix up the section names */
	shstrndx_blob =
	    fu_firmware_get_image_by_idx_bytes(firmware,
					       fu_struct_elf_file_header64le_get_shstrndx(st_fhdr),
					       error);
	if (shstrndx_blob == NULL)
		return FALSE;
	for (guint i = 0; i < sections->len; i++) {
		GByteArray *st_shdr = g_ptr_array_index(sections, i);
		gsize shstrndx_bufsz = 0;
		guint32 sh_name = fu_struct_elf_section_header64le_get_name(st_shdr);
		const gchar *shstrndx_buf = g_bytes_get_data(shstrndx_blob, &shstrndx_bufsz);
		g_autofree gchar *name = NULL;
		g_autoptr(FuFirmware) img = NULL;

		if (sh_name == 0)
			continue;
		if (sh_name > shstrndx_bufsz) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "offset into shstrndx invalid for section 0x%x",
				    i);
			return FALSE;
		}
		img = fu_firmware_get_image_by_idx(firmware, i, error);
		if (img == NULL)
			return FALSE;
		name = g_strndup(shstrndx_buf + sh_name, shstrndx_bufsz - sh_name);
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
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->check_magic = fu_elf_firmware_check_magic;
	klass_firmware->parse = fu_elf_firmware_parse;
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
