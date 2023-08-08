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
#include "fu-ifwi-struct.h"
#include "fu-string.h"

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

#define FU_IFWI_FPT_MAX_ENTRIES	   56

static gboolean
fu_ifwi_fpt_firmware_check_magic(FuFirmware *firmware, GBytes *fw, gsize offset, GError **error)
{
	return fu_struct_ifwi_fpt_validate(g_bytes_get_data(fw, NULL),
					   g_bytes_get_size(fw),
					   offset,
					   error);
}

static gboolean
fu_ifwi_fpt_firmware_parse(FuFirmware *firmware,
			   GBytes *fw,
			   gsize offset,
			   FwupdInstallFlags flags,
			   GError **error)
{
	gsize bufsz = 0;
	guint32 num_of_entries;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);
	g_autoptr(GByteArray) st_hdr = NULL;

	/* sanity check */
	st_hdr = fu_struct_ifwi_fpt_parse(buf, bufsz, offset, error);
	if (st_hdr == NULL)
		return FALSE;
	num_of_entries = fu_struct_ifwi_fpt_get_num_of_entries(st_hdr);
	if (num_of_entries > FU_IFWI_FPT_MAX_ENTRIES) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "invalid FPT number of entries %u",
			    num_of_entries);
		return FALSE;
	}
	if (fu_struct_ifwi_fpt_get_header_version(st_hdr) <
	    FU_STRUCT_IFWI_FPT_DEFAULT_HEADER_VERSION) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "invalid FPT header version: 0x%x",
			    fu_struct_ifwi_fpt_get_header_version(st_hdr));
		return FALSE;
	}

	/* offset by header length */
	offset += fu_struct_ifwi_fpt_get_header_length(st_hdr);

	/* read out entries */
	for (guint i = 0; i < num_of_entries; i++) {
		guint32 data_length;
		guint32 partition_name;
		g_autofree gchar *id = NULL;
		g_autoptr(FuFirmware) img = fu_firmware_new();
		g_autoptr(GByteArray) st_ent = NULL;

		/* read IDX */
		st_ent = fu_struct_ifwi_fpt_entry_parse(buf, bufsz, offset, error);
		if (st_ent == NULL)
			return FALSE;
		partition_name = fu_struct_ifwi_fpt_entry_get_partition_name(st_ent);
		fu_firmware_set_idx(img, partition_name);

		/* convert to text form for convenience */
		id = fu_strsafe((const gchar *)&partition_name, sizeof(partition_name));
		if (id != NULL)
			fu_firmware_set_id(img, id);

		/* get data at offset using zero-copy */
		data_length = fu_struct_ifwi_fpt_entry_get_length(st_ent);
		if (data_length != 0x0) {
			g_autoptr(GBytes) blob = NULL;
			guint32 data_offset = fu_struct_ifwi_fpt_entry_get_offset(st_ent);
			blob = fu_bytes_new_offset(fw, data_offset, data_length, error);
			if (blob == NULL)
				return FALSE;
			fu_firmware_set_bytes(img, blob);
			fu_firmware_set_offset(img, data_offset);
		}
		if (!fu_firmware_add_image_full(firmware, img, error))
			return FALSE;

		/* next */
		offset += st_ent->len;
	}

	/* success */
	return TRUE;
}

static GByteArray *
fu_ifwi_fpt_firmware_write(FuFirmware *firmware, GError **error)
{
	gsize offset = 0;
	g_autoptr(GByteArray) buf = fu_struct_ifwi_fpt_new();
	g_autoptr(GPtrArray) imgs = fu_firmware_get_images(firmware);

	/* fixup the image offsets */
	offset += buf->len;
	offset += FU_STRUCT_IFWI_FPT_ENTRY_SIZE * imgs->len;
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
	fu_struct_ifwi_fpt_set_num_of_entries(buf, imgs->len);

	/* add entries */
	for (guint i = 0; i < imgs->len; i++) {
		FuFirmware *img = g_ptr_array_index(imgs, i);
		g_autoptr(GByteArray) st_ent = fu_struct_ifwi_fpt_entry_new();
		fu_struct_ifwi_fpt_entry_set_partition_name(st_ent, fu_firmware_get_idx(img));
		fu_struct_ifwi_fpt_entry_set_offset(st_ent, fu_firmware_get_offset(img));
		fu_struct_ifwi_fpt_entry_set_length(st_ent, fu_firmware_get_size(img));
		g_byte_array_append(buf, st_ent->data, st_ent->len);
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
	return g_steal_pointer(&buf);
}

static void
fu_ifwi_fpt_firmware_init(FuIfwiFptFirmware *self)
{
	fu_firmware_set_images_max(FU_FIRMWARE(self), FU_IFWI_FPT_MAX_ENTRIES);
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
