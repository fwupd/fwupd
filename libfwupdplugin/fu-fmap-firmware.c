/*
 * Copyright (C) 2020 Benson Leung <bleung@chromium.org>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-byte-array.h"
#include "fu-bytes.h"
#include "fu-common.h"
#include "fu-fmap-firmware.h"
#include "fu-fmap-struct.h"

/**
 * FuFmapFirmware:
 *
 * A FMAP firmware image.
 *
 * See also: [class@FuFirmware]
 */

#define FMAP_AREANAME "FMAP"

G_DEFINE_TYPE(FuFmapFirmware, fu_fmap_firmware, FU_TYPE_FIRMWARE)

static gboolean
fu_fmap_firmware_check_magic(FuFirmware *firmware, GBytes *fw, gsize offset, GError **error)
{
	return fu_struct_fmap_validate(g_bytes_get_data(fw, NULL),
				       g_bytes_get_size(fw),
				       offset,
				       error);
}

static gboolean
fu_fmap_firmware_parse(FuFirmware *firmware,
		       GBytes *fw,
		       gsize offset,
		       FwupdInstallFlags flags,
		       GError **error)
{
	FuFmapFirmwareClass *klass_firmware = FU_FMAP_FIRMWARE_GET_CLASS(firmware);
	gsize bufsz;
	guint32 nareas;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);
	g_autoptr(GByteArray) st_hdr = NULL;

	/* parse */
	st_hdr = fu_struct_fmap_parse(buf, bufsz, offset, error);
	if (st_hdr == NULL)
		return FALSE;
	fu_firmware_set_addr(firmware, fu_struct_fmap_get_base(st_hdr));

	if (fu_struct_fmap_get_size(st_hdr) != bufsz) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "file size incorrect, expected 0x%04x got 0x%04x",
			    fu_struct_fmap_get_size(st_hdr),
			    (guint)bufsz);
		return FALSE;
	}
	nareas = fu_struct_fmap_get_nareas(st_hdr);
	if (nareas < 1) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "number of areas invalid");
		return FALSE;
	}
	offset += st_hdr->len;
	for (gsize i = 0; i < nareas; i++) {
		guint32 area_offset;
		guint32 area_size;
		g_autofree gchar *area_name = NULL;
		g_autoptr(FuFirmware) img = NULL;
		g_autoptr(GByteArray) st_area = NULL;
		g_autoptr(GBytes) bytes = NULL;

		/* load area */
		st_area = fu_struct_fmap_area_parse(buf, bufsz, offset, error);
		if (st_area == NULL)
			return FALSE;
		area_size = fu_struct_fmap_area_get_size(st_area);
		if (area_size == 0)
			continue;
		area_offset = fu_struct_fmap_area_get_offset(st_area);
		bytes = fu_bytes_new_offset(fw, (gsize)area_offset, (gsize)area_size, error);
		if (bytes == NULL)
			return FALSE;
		area_name = fu_struct_fmap_area_get_name(st_area);
		img = fu_firmware_new_from_bytes(bytes);
		fu_firmware_set_id(img, area_name);
		fu_firmware_set_idx(img, i + 1);
		fu_firmware_set_addr(img, area_offset);
		fu_firmware_add_image(firmware, img);

		if (g_strcmp0(area_name, FMAP_AREANAME) == 0) {
			g_autofree gchar *version = NULL;
			version = g_strdup_printf("%d.%d",
						  fu_struct_fmap_get_ver_major(st_hdr),
						  fu_struct_fmap_get_ver_minor(st_hdr));
			fu_firmware_set_version(img, version);
		}
		offset += st_area->len;
	}

	/* subclassed */
	if (klass_firmware->parse != NULL) {
		if (!klass_firmware->parse(firmware, fw, offset, flags, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static GBytes *
fu_fmap_firmware_write(FuFirmware *firmware, GError **error)
{
	gsize total_sz;
	gsize offset;
	g_autoptr(GPtrArray) images = fu_firmware_get_images(firmware);
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GByteArray) st_hdr = fu_struct_fmap_new();

	/* pad to offset */
	if (fu_firmware_get_offset(firmware) > 0)
		fu_byte_array_set_size(buf, fu_firmware_get_offset(firmware), 0x00);

	/* add header */
	total_sz = offset = st_hdr->len + (FU_STRUCT_FMAP_AREA_SIZE * images->len);
	for (guint i = 0; i < images->len; i++) {
		FuFirmware *img = g_ptr_array_index(images, i);
		g_autoptr(GBytes) fw = fu_firmware_get_bytes_with_patches(img, error);
		if (fw == NULL)
			return NULL;
		total_sz += g_bytes_get_size(fw);
	}

	/* header */
	fu_struct_fmap_set_base(st_hdr, fu_firmware_get_addr(firmware));
	fu_struct_fmap_set_nareas(st_hdr, images->len);
	fu_struct_fmap_set_size(st_hdr, fu_firmware_get_offset(firmware) + total_sz);
	g_byte_array_append(buf, st_hdr->data, st_hdr->len);

	/* add each area */
	for (guint i = 0; i < images->len; i++) {
		FuFirmware *img = g_ptr_array_index(images, i);
		g_autoptr(GBytes) fw = fu_firmware_get_bytes_with_patches(img, NULL);
		g_autoptr(GByteArray) st_area = fu_struct_fmap_area_new();
		fu_struct_fmap_area_set_offset(st_area, fu_firmware_get_offset(firmware) + offset);
		fu_struct_fmap_area_set_size(st_area, g_bytes_get_size(fw));
		if (fu_firmware_get_id(img) != NULL) {
			if (!fu_struct_fmap_area_set_name(st_area, fu_firmware_get_id(img), error))
				return NULL;
		}
		g_byte_array_append(buf, st_area->data, st_area->len);
		offset += g_bytes_get_size(fw);
	}

	/* add the images */
	for (guint i = 0; i < images->len; i++) {
		FuFirmware *img = g_ptr_array_index(images, i);
		g_autoptr(GBytes) fw = fu_firmware_get_bytes_with_patches(img, NULL);
		fu_byte_array_append_bytes(buf, fw);
	}

	/* success */
	return g_byte_array_free_to_bytes(g_steal_pointer(&buf));
}

static void
fu_fmap_firmware_init(FuFmapFirmware *self)
{
}

static void
fu_fmap_firmware_class_init(FuFmapFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->check_magic = fu_fmap_firmware_check_magic;
	klass_firmware->parse = fu_fmap_firmware_parse;
	klass_firmware->write = fu_fmap_firmware_write;
}

/**
 * fu_fmap_firmware_new
 *
 * Creates a new #FuFirmware of sub type fmap
 *
 * Since: 1.5.0
 **/
FuFirmware *
fu_fmap_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_FMAP_FIRMWARE, NULL));
}
