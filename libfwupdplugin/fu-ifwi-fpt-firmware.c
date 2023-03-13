/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2022 Intel
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuFirmware"

#include "config.h"

#include <string.h>

#include "fu-byte-array.h"
#include "fu-bytes.h"
#include "fu-ifwi-fpt-firmware.h"
#include "fu-mem.h"
#include "fu-string.h"
#include "fu-struct.h"

/**
 * FuIfwiFptFirmware:
 *
 * An Intel Flash Program Tool (aka FPT) header can be found in IFWI (Integrated Firmware Image)
 * firmware blobs which are used in various Intel products using an IPU (Infrastructure Processing
 * Unit).
 *
 * This could include hardware like SmartNICs, GPUs, camera and audio devices.
 *
 * See also: [class@FuFirmware]
 */

G_DEFINE_TYPE(FuIfwiFptFirmware, fu_ifwi_fpt_firmware, FU_TYPE_FIRMWARE)

#define FU_IFWI_FPT_HEADER_MARKER  0x54504624 /* "$FPT" */
#define FU_IFWI_FPT_HEADER_VERSION 0x20
#define FU_IFWI_FPT_ENTRY_VERSION  0x10
#define FU_IFWI_FPT_MAX_ENTRIES	   56

static gboolean
fu_ifwi_fpt_firmware_check_magic(FuFirmware *firmware, GBytes *fw, gsize offset, GError **error)
{
	guint32 magic = 0;

	if (!fu_memread_uint32_safe(g_bytes_get_data(fw, NULL),
				    g_bytes_get_size(fw),
				    offset,
				    &magic,
				    G_LITTLE_ENDIAN,
				    error)) {
		g_prefix_error(error, "failed to read magic: ");
		return FALSE;
	}
	if (magic != FU_IFWI_FPT_HEADER_MARKER) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "invalid FPT header marker 0x%x, expected 0x%x",
			    magic,
			    (guint)FU_IFWI_FPT_HEADER_MARKER);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_ifwi_fpt_firmware_parse(FuFirmware *firmware,
			   GBytes *fw,
			   gsize offset,
			   FwupdInstallFlags flags,
			   GError **error)
{
	gsize bufsz = 0;
	guint32 num_of_entries = 0;
	guint8 header_length = 0;
	guint8 header_version = 0;
	guint8 entry_version = 0;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);

	/* sanity check */
	if (!fu_struct_unpack_from("<LLBBB",
				   error,
				   buf,
				   bufsz,
				   &offset,
				   NULL, /* header_marker */
				   &num_of_entries,
				   &header_version,
				   &entry_version,
				   &header_length)) /* in bytes */
		return FALSE;
	if (num_of_entries > FU_IFWI_FPT_MAX_ENTRIES) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "invalid FPT number of entries %u",
			    num_of_entries);
		return FALSE;
	}
	if (header_version < FU_IFWI_FPT_HEADER_VERSION) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "invalid FPT header version: 0x%x",
			    header_version);
		return FALSE;
	}
	if (entry_version != FU_IFWI_FPT_ENTRY_VERSION) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "invalid FPT entry version: 0x%x, expected 0x%x",
			    entry_version,
			    (guint)FU_IFWI_FPT_ENTRY_VERSION);
		return FALSE;
	}

	/* offset by header length */
	offset += header_length;

	/* read out entries */
	for (guint i = 0; i < num_of_entries; i++) {
		guint32 data_length = 0;
		guint32 data_offset = 0;
		guint32 partition_name = 0;
		g_autofree gchar *id = NULL;
		g_autoptr(FuFirmware) img = fu_firmware_new();

		if (!fu_struct_unpack_from("<[L4xLL12xL]",
					   error,
					   buf,
					   bufsz,
					   &offset,
					   &partition_name,
					   &data_offset,
					   &data_length,
					   NULL)) /* partition_type: 0=code, 1=data, 2=GLUT */
			return FALSE;

		/* convert to text form for convenience */
		fu_firmware_set_idx(img, partition_name);
		id = fu_strsafe((const gchar *)&partition_name, sizeof(partition_name));
		if (id != NULL)
			fu_firmware_set_id(img, id);

		/* get data at offset using zero-copy */
		if (data_length != 0x0) {
			g_autoptr(GBytes) blob = NULL;
			blob = fu_bytes_new_offset(fw, data_offset, data_length, error);
			if (blob == NULL)
				return FALSE;
			fu_firmware_set_bytes(img, blob);
			fu_firmware_set_offset(img, data_offset);
		}
		fu_firmware_add_image(firmware, img);
	}

	/* success */
	return TRUE;
}

static GBytes *
fu_ifwi_fpt_firmware_write(FuFirmware *firmware, GError **error)
{
	gsize offset = 0;
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(GPtrArray) imgs = fu_firmware_get_images(firmware);

	/* fixup the image offsets */
	offset += fu_struct_size("<LLBBBBHHLLHHHH", NULL);
	offset += fu_struct_size("<L4xLL12xL", NULL) * imgs->len;
	for (guint i = 0; i < imgs->len; i++) {
		FuFirmware *img = g_ptr_array_index(imgs, i);
		g_autoptr(GBytes) blob = NULL;
		blob = fu_firmware_get_bytes(img, error);
		if (blob == NULL) {
			g_prefix_error(error, "image 0x%x: ", i);
			return NULL;
		}
		fu_firmware_set_offset(img, offset);
		offset += g_bytes_get_size(blob);
	}

	/* write the header */
	buf = fu_struct_pack("<LLBBBBHHLLHHHH",
			     error,
			     FU_IFWI_FPT_HEADER_MARKER,
			     imgs->len,
			     FU_IFWI_FPT_HEADER_VERSION,
			     FU_IFWI_FPT_ENTRY_VERSION,
			     fu_struct_size("<LLBBBBHHLLHHHH", NULL),
			     0x0,  /* flags */
			     0x0,  /* ticks_to_add */
			     0x0,  /* tokens_to_add */
			     0x0,  /* uma_size */
			     0x0,  /* crc32 */
			     0x0,  /* fitc_major */
			     0x0,  /* fitc_minor */
			     0x0,  /* fitc_hotfix */
			     0x0); /* fitc_build */
	if (buf == NULL)
		return NULL;

	/* add entries */
	for (guint i = 0; i < imgs->len; i++) {
		FuFirmware *img = g_ptr_array_index(imgs, i);
		g_autoptr(GByteArray) buf_entry = NULL;
		buf_entry = fu_struct_pack("<L4xLL12xL",
					   error,
					   (guint)fu_firmware_get_idx(img),
					   (guint)fu_firmware_get_offset(img),
					   fu_firmware_get_size(img),
					   0x0); /* partition_type */
		if (buf_entry == NULL)
			return NULL;
		g_byte_array_append(buf, buf_entry->data, buf_entry->len);
	}

	/* add entry data */
	for (guint i = 0; i < imgs->len; i++) {
		FuFirmware *img = g_ptr_array_index(imgs, i);
		g_autoptr(GBytes) blob = NULL;
		blob = fu_firmware_get_bytes(img, error);
		if (blob == NULL)
			return NULL;
		fu_byte_array_append_bytes(buf, blob);
	}

	/* success */
	return g_byte_array_free_to_bytes(g_steal_pointer(&buf));
}

static void
fu_ifwi_fpt_firmware_init(FuIfwiFptFirmware *self)
{
}

static void
fu_ifwi_fpt_firmware_class_init(FuIfwiFptFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->check_magic = fu_ifwi_fpt_firmware_check_magic;
	klass_firmware->parse = fu_ifwi_fpt_firmware_parse;
	klass_firmware->write = fu_ifwi_fpt_firmware_write;
}

/**
 * fu_ifwi_fpt_firmware_new:
 *
 * Creates a new #FuFirmware of Intel Flash Program Tool format
 *
 * Since: 1.8.2
 **/
FuFirmware *
fu_ifwi_fpt_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_IFWI_FPT_FIRMWARE, NULL));
}
