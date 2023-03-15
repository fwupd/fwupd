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

#define FU_IFWI_FPT_HEADER_VERSION 0x20
#define FU_IFWI_FPT_ENTRY_VERSION  0x10
#define FU_IFWI_FPT_MAX_ENTRIES	   56

static gboolean
fu_ifwi_fpt_firmware_check_magic(FuFirmware *firmware, GBytes *fw, gsize offset, GError **error)
{
	FuStruct *st_hdr = fu_struct_lookup(firmware, "IfwiFptHeader");
	return fu_struct_unpack_full(st_hdr,
				     g_bytes_get_data(fw, NULL),
				     g_bytes_get_size(fw),
				     offset,
				     FU_STRUCT_FLAG_ONLY_CONSTANTS,
				     error);
}

static gboolean
fu_ifwi_fpt_firmware_parse(FuFirmware *firmware,
			   GBytes *fw,
			   gsize offset,
			   FwupdInstallFlags flags,
			   GError **error)
{
	FuStruct *st_hdr = fu_struct_lookup(firmware, "IfwiFptHeader");
	FuStruct *st_ent = fu_struct_lookup(firmware, "IfwiFptEntry");
	gsize bufsz = 0;
	guint32 num_of_entries;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);

	/* sanity check */
	if (!fu_struct_unpack_full(st_hdr, buf, bufsz, offset, FU_STRUCT_FLAG_NONE, error))
		return FALSE;
	num_of_entries = fu_struct_get_u32(st_hdr, "num_of_entries");
	if (num_of_entries > FU_IFWI_FPT_MAX_ENTRIES) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "invalid FPT number of entries %u",
			    num_of_entries);
		return FALSE;
	}
	if (fu_struct_get_u8(st_hdr, "header_version") < FU_IFWI_FPT_HEADER_VERSION) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "invalid FPT header version: 0x%x",
			    fu_struct_get_u8(st_hdr, "header_version"));
		return FALSE;
	}
	if (fu_struct_get_u8(st_hdr, "entry_version") != FU_IFWI_FPT_ENTRY_VERSION) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "invalid FPT entry version: 0x%x, expected 0x%x",
			    fu_struct_get_u8(st_hdr, "entry_version"),
			    (guint)FU_IFWI_FPT_ENTRY_VERSION);
		return FALSE;
	}

	/* offset by header length */
	offset += fu_struct_get_u8(st_hdr, "header_length");

	/* read out entries */
	for (guint i = 0; i < num_of_entries; i++) {
		guint32 data_length;
		guint32 partition_name;
		g_autofree gchar *id = NULL;
		g_autoptr(FuFirmware) img = fu_firmware_new();

		/* read IDX */
		if (!fu_struct_unpack_full(st_ent, buf, bufsz, offset, FU_STRUCT_FLAG_NONE, error))
			return FALSE;
		partition_name = fu_struct_get_u32(st_ent, "partition_name");
		fu_firmware_set_idx(img, partition_name);

		/* convert to text form for convenience */
		id = fu_strsafe((const gchar *)&partition_name, sizeof(partition_name));
		if (id != NULL)
			fu_firmware_set_id(img, id);

		/* get data at offset using zero-copy */
		data_length = fu_struct_get_u32(st_ent, "length");
		if (data_length != 0x0) {
			g_autoptr(GBytes) blob = NULL;
			guint32 data_offset = fu_struct_get_u32(st_ent, "offset");
			blob = fu_bytes_new_offset(fw, data_offset, data_length, error);
			if (blob == NULL)
				return FALSE;
			fu_firmware_set_bytes(img, blob);
			fu_firmware_set_offset(img, data_offset);
		}
		fu_firmware_add_image(firmware, img);

		/* next */
		offset += fu_struct_size(st_ent);
	}

	/* success */
	return TRUE;
}

static GBytes *
fu_ifwi_fpt_firmware_write(FuFirmware *firmware, GError **error)
{
	FuStruct *st_hdr = fu_struct_lookup(firmware, "IfwiFptHeader");
	FuStruct *st_ent = fu_struct_lookup(firmware, "IfwiFptEntry");
	gsize offset = 0;
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(GPtrArray) imgs = fu_firmware_get_images(firmware);

	/* fixup the image offsets */
	offset += fu_struct_size(st_hdr);
	offset += fu_struct_size(st_ent) * imgs->len;
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
	fu_struct_set_u32(st_hdr, "num_of_entries", imgs->len);
	buf = fu_struct_pack(st_hdr);

	/* add entries */
	for (guint i = 0; i < imgs->len; i++) {
		FuFirmware *img = g_ptr_array_index(imgs, i);
		fu_struct_set_u32(st_ent, "partition_name", fu_firmware_get_idx(img));
		fu_struct_set_u32(st_ent, "offset", fu_firmware_get_offset(img));
		fu_struct_set_u32(st_ent, "length", fu_firmware_get_size(img));
		fu_struct_pack_into(st_ent, buf);
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
	fu_struct_register(self,
			   "IfwiFptHeader {"
			   "    header_marker: u32le:: 0x54504624,"
			   "    num_of_entries: u32le,"
			   "    header_version: u8: 0x20,"
			   "    entry_version: u8: 0x10,"
			   "    header_length: u8: $struct_size,"
			   "    flags: u8,"
			   "    ticks_to_add: u16le,"
			   "    tokens_to_add: u16le,"
			   "    uma_size: u32le,"
			   "    crc32: u32le,"
			   "    fitc_major: u16le,"
			   "    fitc_minor: u16le,"
			   "    fitc_hotfix: u16le,"
			   "    fitc_build: u16le,"
			   "}");
	fu_struct_register(self,
			   "IfwiFptEntry {"
			   "    partition_name: u32le,"
			   "    reserved1: 4u8,"
			   "    offset: u32le,"
			   "    length: u32le," /* bytes */
			   "    reserved2: 12u8,"
			   "    partition_type: u32le," /* 0 for code, 1 for data, 2 for GLUT */
			   "}");
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
