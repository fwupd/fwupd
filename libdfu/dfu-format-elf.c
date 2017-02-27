/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015-2016 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include "config.h"

#include <fcntl.h>
#include <gio/gunixinputstream.h>

#ifdef HAVE_LIBELF
#include <gelf.h>
#include <libelf.h>
#include <linux/memfd.h>
#endif

#include <string.h>
#include <sys/syscall.h>

#include "dfu-firmware-private.h"
#include "dfu-format-elf.h"
#include "dfu-error.h"

/**
 * dfu_firmware_detect_elf: (skip)
 * @bytes: data to parse
 *
 * Attempts to sniff the data and work out the firmware format
 *
 * Returns: a #DfuFirmwareFormat, e.g. %DFU_FIRMWARE_FORMAT_ELF
 **/
DfuFirmwareFormat
dfu_firmware_detect_elf (GBytes *bytes)
{
	guint8 *data;
	gsize len;

	/* check data size */
	data = (guint8 *) g_bytes_get_data (bytes, &len);
	if (len < 16)
		return DFU_FIRMWARE_FORMAT_UNKNOWN;

	/* sniff the signature bytes */
	if (memcmp (data + 1, "ELF", 3) != 0)
		return DFU_FIRMWARE_FORMAT_UNKNOWN;

	/* success */
	return DFU_FIRMWARE_FORMAT_ELF;
}

#ifdef HAVE_LIBELF
static DfuElement *
_get_element_from_section_name (Elf *e, const gchar *desired_name)
{
	DfuElement *element = NULL;
	Elf_Scn *scn = NULL;
	GElf_Shdr shdr;
	const gchar *name;
	size_t shstrndx;

	if (elf_getshdrstrndx (e, &shstrndx) != 0) {
		g_warning ("failed elf_getshdrstrndx");
		return NULL;
	}
	while ((scn = elf_nextscn (e, scn)) != NULL ) {
		if (gelf_getshdr (scn, &shdr ) != & shdr) {
			g_warning ("failed gelf_getshdr");
			continue;
		}

		/* not program data */
		if (shdr.sh_type != SHT_PROGBITS)
			continue;

		/* not the same section name */
		if ((name = elf_strptr (e, shstrndx, shdr.sh_name)) == NULL) {
			g_warning ("failed elf_strptr");
			continue;
		}
		if (g_strcmp0 (name, desired_name) == 0) {
			Elf_Data *data = elf_getdata (scn, NULL);
			if (data != NULL && data->d_buf != NULL) {
				g_autoptr(GBytes) bytes = NULL;
				bytes = g_bytes_new (data->d_buf, data->d_size);
				element = dfu_element_new ();
				dfu_element_set_contents (element, bytes);
				dfu_element_set_address (element, shdr.sh_addr);
			}
			break;
		}
	}
	return element;
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(Elf, elf_end);

static void
dfu_format_elf_symbols_from_symtab (DfuFirmware *firmware, Elf *e)
{
	Elf_Scn *scn = NULL;
	gsize shstrndx;

	if (elf_getshdrstrndx (e, &shstrndx) != 0) {
		g_warning ("failed elf_getshdrstrndx");
		return;
	}
	while ((scn = elf_nextscn (e, scn)) != NULL ) {
		Elf_Data *data;
		GElf_Shdr shdr;
		const gchar *name;
		gssize ns;
		if (gelf_getshdr (scn, &shdr) != &shdr) {
			g_warning ("failed gelf_getshdr");
			continue;
		}

		/* not program data */
		if (shdr.sh_type != SHT_SYMTAB)
			continue;

		/* get symbols */
		data = elf_getdata (scn, NULL);
		ns = shdr.sh_size / shdr.sh_entsize;
		for (gint i = 0; i < ns; i++) {
			GElf_Sym sym;
                        gelf_getsym (data, i, &sym);
			if (sym.st_value == 0)
				continue;
			name = elf_strptr (e, shdr.sh_link, sym.st_name);
			if (name == NULL)
				continue;
			dfu_firmware_add_symbol (firmware, name, sym.st_value);
		}
	}
}
#endif

/**
 * dfu_firmware_from_elf: (skip)
 * @firmware: a #DfuFirmware
 * @bytes: data to parse
 * @flags: some #DfuFirmwareParseFlags
 * @error: a #GError, or %NULL
 *
 * Unpacks into a firmware object from ELF data.
 *
 * Returns: %TRUE for success
 **/
gboolean
dfu_firmware_from_elf (DfuFirmware *firmware,
		       GBytes *bytes,
		       DfuFirmwareParseFlags flags,
		       GError **error)
{
#ifdef HAVE_LIBELF
	guint i;
	guint sections_cnt = 0;
	g_autoptr(Elf) e = NULL;
	const gchar *section_names[] = {
		".interrupt",
		".text",
		NULL };

	/* load library */
	if (elf_version (EV_CURRENT) == EV_NONE) {
		g_set_error (error,
			     DFU_ERROR,
			     DFU_ERROR_INTERNAL,
			     "ELF library init failed: %s",
			     elf_errmsg (-1));
		return FALSE;
	}

	/* parse data */
	e = elf_memory ((gchar *) g_bytes_get_data (bytes, NULL),
			g_bytes_get_size (bytes));
	if (e == NULL) {
		g_set_error (error,
			     DFU_ERROR,
			     DFU_ERROR_INTERNAL,
			     "failed to load data as ELF: %s",
			     elf_errmsg (-1));
		return FALSE;
	}
	if (elf_kind (e) != ELF_K_ELF) {
		g_set_error (error,
			     DFU_ERROR,
			     DFU_ERROR_INTERNAL,
			     "not a supported ELF format: %s",
			     elf_errmsg (-1));
		return FALSE;
	}
	g_debug ("loading %ib ELF object" ,
		 gelf_getclass (e) == ELFCLASS32 ? 32 : 64);

	/* add interesting sections as the image */
	for (i = 0; section_names[i] != NULL; i++) {
		g_autoptr(DfuElement) element = NULL;
		g_autoptr(DfuImage) image = NULL;
		element = _get_element_from_section_name (e, section_names[i]);
		if (element == NULL)
			continue;
		image = dfu_image_new ();
		dfu_image_add_element (image, element);
		dfu_image_set_name (image, section_names[i]);
		dfu_firmware_add_image (firmware, image);
		sections_cnt++;
	}

	/* load symbol table */
	dfu_format_elf_symbols_from_symtab (firmware, e);

	/* nothing found */
	if (sections_cnt == 0) {
		g_set_error_literal (error,
				     DFU_ERROR,
				     DFU_ERROR_INTERNAL,
				     "no firmware found in ELF file");
		return FALSE;
	}

	/* success */
	return TRUE;
#else
	g_set_error_literal (error,
			     DFU_ERROR,
			     DFU_ERROR_INTERNAL,
			     "compiled without libelf support");
	return FALSE;
#endif
}

#ifdef HAVE_LIBELF
static int
_memfd_create (const char *name, unsigned int flags)
{
#if defined (__NR_memfd_create)
	return syscall (__NR_memfd_create, name, flags);
#else
	return -1;
#endif
}

static gboolean
dfu_format_elf_pack_element (Elf *e, DfuElement *element, GError **error)
{
	Elf32_Shdr *shdr;
	Elf_Data *data;
	Elf_Scn *scn;
	GBytes *bytes = dfu_element_get_contents (element);

	/* create a section descriptor for the firmware */
	scn = elf_newscn (e);
	if (scn == NULL) {
		g_set_error (error,
			     DFU_ERROR,
			     DFU_ERROR_INTERNAL,
			     "failed to create section descriptor: %s",
			     elf_errmsg (-1));
		return FALSE;
	}
	data = elf_newdata (scn);
	data->d_align = 1;
	data->d_off = 0;
	data->d_buf = (gpointer) g_bytes_get_data (bytes, NULL);
	data->d_type = ELF_T_BYTE;
	data->d_size = g_bytes_get_size (bytes);
	data->d_version = EV_CURRENT;
	shdr = elf32_getshdr (scn);
	if (shdr == NULL) {
		g_set_error (error,
			     DFU_ERROR,
			     DFU_ERROR_INTERNAL,
			     "failed to create XXX: %s",
			     elf_errmsg (-1));
		return FALSE;
	}
	shdr->sh_name = 1;
	shdr->sh_type = SHT_PROGBITS;
	shdr->sh_flags = SHF_ALLOC;
	shdr->sh_entsize = 0;
	return TRUE;
}

static gboolean
dfu_format_elf_pack_image (Elf *e, DfuImage *image, GError **error)
{
	DfuElement *element;

	/* only works for one element */
	element = dfu_image_get_element_default (image);
	if (element == NULL) {
		g_set_error_literal (error,
				     DFU_ERROR,
				     DFU_ERROR_INTERNAL,
				     "no element to write");
		return FALSE;
	}
	return dfu_format_elf_pack_element (e, element, error);
}
#endif

/**
 * dfu_firmware_to_elf: (skip)
 * @firmware: a #DfuFirmware
 * @error: a #GError, or %NULL
 *
 * Packs elf firmware
 *
 * Returns: (transfer full): the packed data
 **/
GBytes *
dfu_firmware_to_elf (DfuFirmware *firmware, GError **error)
{
#ifdef HAVE_LIBELF
	DfuImage *image;
	Elf32_Ehdr *ehdr;
	Elf32_Shdr *shdr;
	Elf_Data *data;
	Elf_Scn *scn;
	gint fd;
	goffset fsize;
	g_autoptr(Elf) e = NULL;
	g_autoptr(GInputStream) stream = NULL;
	gchar string_table2[] =
		"\0"
		".text\0" // FIXME: use the name in the DfuImage?
		".shstrtab";

	/* only works for one image */
	image = dfu_firmware_get_image_default (firmware);
	if (image == NULL) {
		g_set_error_literal (error,
				     DFU_ERROR,
				     DFU_ERROR_INTERNAL,
				     "no image to write");
		return FALSE;
	}

	/* load library */
	if (elf_version (EV_CURRENT) == EV_NONE) {
		g_set_error (error,
			     DFU_ERROR,
			     DFU_ERROR_INTERNAL,
			     "ELF library init failed: %s",
			     elf_errmsg (-1));
		return FALSE;
	}

	/* create from buffer */
	fd = _memfd_create ("elf", MFD_CLOEXEC);
	if (fd < 0) {
		g_set_error_literal (error,
				     DFU_ERROR,
				     DFU_ERROR_INTERNAL,
				     "failed to open memfd");
		return NULL;
	}
	stream = g_unix_input_stream_new (fd, TRUE);
	e = elf_begin (fd, ELF_C_WRITE, NULL);
	if (e == NULL) {
		g_set_error (error,
			     DFU_ERROR,
			     DFU_ERROR_INTERNAL,
			     "failed to create ELF: %s",
			     elf_errmsg (-1));
		return NULL;
	}

	/* add executable header */
	ehdr = elf32_newehdr (e);
	if (ehdr == NULL) {
		g_warning ("failed to create executable header: %s",
			     elf_errmsg (-1));
		return NULL;
	}
	ehdr->e_ident[EI_DATA] = ELFDATA2LSB;
	ehdr->e_machine = EM_NONE;
	ehdr->e_type = ET_NONE;

	/* pack the image */
	if (!dfu_format_elf_pack_image (e, image, error))
		return FALSE;

	/* allocate section for holding the string table */
	scn = elf_newscn (e);
	if (scn == NULL) {
		g_set_error (error,
			     DFU_ERROR,
			     DFU_ERROR_INTERNAL,
			     "failed to create section descriptor: %s",
			     elf_errmsg (-1));
		return NULL;
	}
	data = elf_newdata (scn);
	data->d_align = 1;
	data->d_off = 0;
	data->d_buf = string_table2;
	data->d_type = ELF_T_BYTE;
	data->d_size = sizeof (string_table2);
	data->d_version = EV_CURRENT;
	shdr = elf32_getshdr (scn);
	if (shdr == NULL) {
		g_set_error (error,
			     DFU_ERROR,
			     DFU_ERROR_INTERNAL,
			     "failed to create XXX: %s",
			     elf_errmsg (-1));
		return NULL;
	}
	shdr->sh_name = 7; /* offset to table name */
	shdr->sh_type = SHT_STRTAB;
	shdr->sh_flags = SHF_STRINGS | SHF_ALLOC;
	shdr->sh_entsize = 0;

	/* set string table index field */
	ehdr->e_shstrndx = elf_ndxscn (scn);

	/* compute the layout of the object */
	if (elf_update (e, ELF_C_NULL) < 0) {
		g_set_error (error,
			     DFU_ERROR,
			     DFU_ERROR_INTERNAL,
			     "failed to compute layout: %s",
			     elf_errmsg (-1));
		return NULL;
	}

	/* write out the actual data */
	elf_flagphdr (e, ELF_C_SET, ELF_F_DIRTY );
	if (elf_update (e, ELF_C_WRITE ) < 0) {
		g_set_error (error,
			     DFU_ERROR,
			     DFU_ERROR_INTERNAL,
			     "failed to write to fd: %s",
			     elf_errmsg (-1));
		return NULL;
	}

	/* read out the blob of memory in one chunk */
	fsize = lseek(fd, 0, SEEK_END);
	if (lseek (fd, 0, SEEK_SET) < 0) {
		g_set_error (error,
			     DFU_ERROR,
			     DFU_ERROR_INTERNAL,
			     "failed to seek to start");
		return NULL;
	}
	return g_input_stream_read_bytes (stream, fsize, NULL, error);
#else
	g_set_error_literal (error,
			     DFU_ERROR,
			     DFU_ERROR_INTERNAL,
			     "compiled without libelf support");
	return FALSE;
#endif
}
