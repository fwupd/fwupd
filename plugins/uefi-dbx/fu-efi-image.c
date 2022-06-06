/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-efi-image.h"

struct _FuEfiImage {
	GObject parent_instance;
	gchar *checksum;
};

typedef struct {
	gsize offset;
	gsize size;
	gchar *name;
} FuEfiImageRegion;

typedef struct __attribute__((packed)) {
	guint32 addr;
	guint32 size;
} FuEfiImageDataDirEntry;

G_DEFINE_TYPE(FuEfiImage, fu_efi_image, G_TYPE_OBJECT)

#define _DOS_OFFSET_SIGNATURE	 0x00
#define _DOS_OFFSET_TO_PE_HEADER 0x3c

#define _PEI_OFFSET_SIGNATURE		 0x00
#define _PEI_OFFSET_MACHINE		 0x04
#define _PEI_OFFSET_NUMBER_OF_SECTIONS	 0x06
#define _PEI_OFFSET_OPTIONAL_HEADER_SIZE 0x14
#define _PEI_HEADER_SIZE		 0x18

#define _PE_OFFSET_SIZE_OF_HEADERS    0x54
#define _PE_OFFSET_CHECKSUM	      0x58
#define _PE_OFFSET_DEBUG_TABLE_OFFSET 0x98

#define _PEP_OFFSET_SIZE_OF_HEADERS    0x54
#define _PEP_OFFSET_CHECKSUM	       0x58
#define _PEP_OFFSET_DEBUG_TABLE_OFFSET 0xa8

#define _SECTION_HEADER_OFFSET_NAME 0x0
#define _SECTION_HEADER_OFFSET_SIZE 0x10
#define _SECTION_HEADER_OFFSET_PTR  0x14
#define _SECTION_HEADER_SIZE	    0x28

#define IMAGE_FILE_MACHINE_AMD64   0x8664
#define IMAGE_FILE_MACHINE_I386	   0x014c
#define IMAGE_FILE_MACHINE_THUMB   0x01c2
#define IMAGE_FILE_MACHINE_AARCH64 0xaa64

static gint
fu_efi_image_region_sort_cb(gconstpointer a, gconstpointer b)
{
	const FuEfiImageRegion *r1 = *((const FuEfiImageRegion **)a);
	const FuEfiImageRegion *r2 = *((const FuEfiImageRegion **)b);
	if (r1->offset < r2->offset)
		return -1;
	if (r1->offset > r2->offset)
		return 1;
	return 0;
}

static FuEfiImageRegion *
fu_efi_image_add_region(GPtrArray *checksum_regions,
			const gchar *name,
			gsize offset_start,
			gsize offset_end)
{
	FuEfiImageRegion *r = g_new0(FuEfiImageRegion, 1);
	r->name = g_strdup(name);
	r->offset = offset_start;
	r->size = offset_end - offset_start;
	g_ptr_array_add(checksum_regions, r);
	return r;
}

static void
fu_efi_image_region_free(FuEfiImageRegion *r)
{
	g_free(r->name);
	g_free(r);
}

FuEfiImage *
fu_efi_image_new(GBytes *data, GError **error)
{
	FuEfiImageRegion *r;
	const guint8 *buf;
	gsize bufsz;
	gsize image_bytes = 0;
	gsize checksum_offset;
	gsize data_dir_debug_offset;
	gsize offset_tmp;
	guint16 dos_sig = 0;
	guint16 machine = 0;
	guint16 opthdrsz;
	guint16 sections;
	guint32 baseaddr = 0;
	guint32 cert_table_size;
	guint32 header_size;
	guint32 nt_sig = 0;
	g_autoptr(FuEfiImage) self = g_object_new(FU_TYPE_EFI_IMAGE, NULL);
	g_autoptr(GChecksum) checksum = g_checksum_new(G_CHECKSUM_SHA256);
	g_autoptr(GPtrArray) checksum_regions = NULL;

	/* verify this is a DOS file */
	buf = fu_bytes_get_data_safe(data, &bufsz, error);
	if (buf == NULL)
		return NULL;
	if (!fu_memread_uint16_safe(buf,
				    bufsz,
				    _DOS_OFFSET_SIGNATURE,
				    &dos_sig,
				    G_LITTLE_ENDIAN,
				    error))
		return NULL;
	if (dos_sig != 0x5a4d) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "Invalid DOS header magic %04x",
			    dos_sig);
		return NULL;
	}

	/* verify the PE signature */
	if (!fu_memread_uint32_safe(buf,
				    bufsz,
				    _DOS_OFFSET_TO_PE_HEADER,
				    &baseaddr,
				    G_LITTLE_ENDIAN,
				    error))
		return NULL;
	if (!fu_memread_uint32_safe(buf,
				    bufsz,
				    baseaddr + _PEI_OFFSET_SIGNATURE,
				    &nt_sig,
				    G_LITTLE_ENDIAN,
				    error))
		return NULL;
	if (nt_sig != 0x4550) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "Invalid PE header signature %08x",
			    nt_sig);
		return NULL;
	}

	/* which machine type are we reading */
	if (!fu_memread_uint16_safe(buf,
				    bufsz,
				    baseaddr + _PEI_OFFSET_MACHINE,
				    &machine,
				    G_LITTLE_ENDIAN,
				    error))
		return NULL;
	if (machine == IMAGE_FILE_MACHINE_AMD64 || machine == IMAGE_FILE_MACHINE_AARCH64) {
		/* a.out header directly follows PE header */
		if (!fu_memread_uint16_safe(buf,
					    bufsz,
					    baseaddr + _PEI_HEADER_SIZE,
					    &machine,
					    G_LITTLE_ENDIAN,
					    error))
			return NULL;
		if (machine != 0x020b) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_SUPPORTED,
				    "Invalid a.out machine type %04x",
				    machine);
			return NULL;
		}
		if (!fu_memread_uint32_safe(buf,
					    bufsz,
					    baseaddr + _PEP_OFFSET_SIZE_OF_HEADERS,
					    &header_size,
					    G_LITTLE_ENDIAN,
					    error))
			return NULL;

		checksum_offset = baseaddr + _PEP_OFFSET_CHECKSUM;

		/* now, this is odd. sbsigntools seems to think that we're
		 * skipping the CertificateTable -- but we actually seems to be
		 * ignoring Debug instead */
		data_dir_debug_offset = baseaddr + _PEP_OFFSET_DEBUG_TABLE_OFFSET;

	} else if (machine == IMAGE_FILE_MACHINE_I386 || machine == IMAGE_FILE_MACHINE_THUMB) {
		/* a.out header directly follows PE header */
		if (!fu_memread_uint16_safe(buf,
					    bufsz,
					    baseaddr + _PEI_HEADER_SIZE,
					    &machine,
					    G_LITTLE_ENDIAN,
					    error))
			return NULL;
		if (machine != 0x010b) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_SUPPORTED,
				    "Invalid a.out machine type %04x",
				    machine);
			return NULL;
		}
		if (!fu_memread_uint32_safe(buf,
					    bufsz,
					    baseaddr + _PE_OFFSET_SIZE_OF_HEADERS,
					    &header_size,
					    G_LITTLE_ENDIAN,
					    error))
			return NULL;

		checksum_offset = baseaddr + _PE_OFFSET_CHECKSUM;
		data_dir_debug_offset = baseaddr + _PE_OFFSET_DEBUG_TABLE_OFFSET;

	} else {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "Invalid PE header machine %04x",
			    machine);
		return NULL;
	}

	/* get sections */
	if (!fu_memread_uint32_safe(buf,
				    bufsz,
				    data_dir_debug_offset + sizeof(guint32),
				    &cert_table_size,
				    G_LITTLE_ENDIAN,
				    error))
		return NULL;
	if (!fu_memread_uint16_safe(buf,
				    bufsz,
				    baseaddr + _PEI_OFFSET_NUMBER_OF_SECTIONS,
				    &sections,
				    G_LITTLE_ENDIAN,
				    error))
		return NULL;
	g_debug("number_of_sections: %u", sections);

	/* get header size */
	if (!fu_memread_uint16_safe(buf,
				    bufsz,
				    baseaddr + _PEI_OFFSET_OPTIONAL_HEADER_SIZE,
				    &opthdrsz,
				    G_LITTLE_ENDIAN,
				    error))
		return NULL;
	g_debug("optional_header_size: 0x%x", opthdrsz);

	/* first region: beginning to checksum_offset field */
	checksum_regions = g_ptr_array_new_with_free_func((GDestroyNotify)fu_efi_image_region_free);
	r = fu_efi_image_add_region(checksum_regions, "begin->cksum", 0x0, checksum_offset);
	image_bytes += r->size + sizeof(guint32);

	/* second region: end of checksum_offset to certificate table entry */
	r = fu_efi_image_add_region(checksum_regions,
				    "cksum->datadir[DEBUG]",
				    checksum_offset + sizeof(guint32),
				    data_dir_debug_offset);
	image_bytes += r->size + sizeof(FuEfiImageDataDirEntry);

	/* third region: end of checksum_offset to end of headers */
	r = fu_efi_image_add_region(checksum_regions,
				    "datadir[DEBUG]->headers",
				    data_dir_debug_offset + sizeof(FuEfiImageDataDirEntry),
				    header_size);
	image_bytes += r->size;

	/* add COFF sections */
	offset_tmp = baseaddr + _PEI_HEADER_SIZE + opthdrsz;
	for (guint i = 0; i < sections; i++) {
		guint32 file_offset = 0;
		guint32 file_size = 0;
		gchar name[9] = {'\0'};

		if (!fu_memread_uint32_safe(buf,
					    bufsz,
					    offset_tmp + _SECTION_HEADER_OFFSET_PTR,
					    &file_offset,
					    G_LITTLE_ENDIAN,
					    error))
			return NULL;
		if (!fu_memread_uint32_safe(buf,
					    bufsz,
					    offset_tmp + _SECTION_HEADER_OFFSET_SIZE,
					    &file_size,
					    G_LITTLE_ENDIAN,
					    error))
			return NULL;
		if (file_size == 0)
			continue;
		if (!fu_memcpy_safe((guint8 *)name,
				    sizeof(name),
				    0x0, /* dst */
				    buf,
				    bufsz,
				    offset_tmp + _SECTION_HEADER_OFFSET_NAME, /* src */
				    sizeof(name) - 1,
				    error))
			return NULL;
		r = fu_efi_image_add_region(checksum_regions,
					    name,
					    file_offset,
					    file_offset + file_size);
		image_bytes += r->size;

		if (file_offset + r->size > bufsz) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "file-aligned section %s extends beyond end of file",
				    r->name);
			return NULL;
		}
		offset_tmp += _SECTION_HEADER_SIZE;
	}

	/* make sure in order */
	g_ptr_array_sort(checksum_regions, fu_efi_image_region_sort_cb);

	/* for the data at the end of the image */
	if (image_bytes + cert_table_size < bufsz) {
		fu_efi_image_add_region(checksum_regions,
					"endjunk",
					image_bytes,
					bufsz - cert_table_size);
	} else if (image_bytes + cert_table_size > bufsz) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "checksum_offset areas outside image size");
		return NULL;
	}

	/* calculate the checksum we would find in the dbx */
	for (guint i = 0; i < checksum_regions->len; i++) {
		r = g_ptr_array_index(checksum_regions, i);
		g_debug("region %s: 0x%04x -> 0x%04x [0x%04x]",
			r->name,
			(guint)r->offset,
			(guint)(r->offset + r->size - 1),
			(guint)r->size);
		g_checksum_update(checksum, (const guchar *)buf + r->offset, (gssize)r->size);
	}
	self->checksum = g_strdup(g_checksum_get_string(checksum));
	return g_steal_pointer(&self);
}

const gchar *
fu_efi_image_get_checksum(FuEfiImage *self)
{
	return self->checksum;
}

static void
fu_efi_image_finalize(GObject *obj)
{
	FuEfiImage *self = FU_EFI_IMAGE(obj);
	g_free(self->checksum);
	G_OBJECT_CLASS(fu_efi_image_parent_class)->finalize(obj);
}

static void
fu_efi_image_class_init(FuEfiImageClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_efi_image_finalize;
}

static void
fu_efi_image_init(FuEfiImage *self)
{
}
