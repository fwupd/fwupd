/*
 * Copyright 2020 Benson Leung <bleung@chromium.org>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-byte-array.h"
#include "fu-bytes.h"
#include "fu-common.h"
#include "fu-fmap-firmware.h"
#include "fu-fmap-struct.h"
#include "fu-input-stream.h"
#include "fu-partial-input-stream.h"

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
fu_fmap_firmware_parse(FuFirmware *firmware,
		       GInputStream *stream,
		       FuFirmwareParseFlags flags,
		       GError **error)
{
	gsize offset = 0;
	gsize streamsz = 0;
	guint32 nareas;
	g_autoptr(GByteArray) st_hdr = NULL;

	/* find the magic token */
	if (!fu_input_stream_find(stream,
				  (const guint8 *)FU_STRUCT_FMAP_DEFAULT_SIGNATURE,
				  FU_STRUCT_FMAP_SIZE_SIGNATURE,
				  &offset,
				  error))
		return FALSE;

	/* parse */
	st_hdr = fu_struct_fmap_parse_stream(stream, offset, error);
	if (st_hdr == NULL)
		return FALSE;
	fu_firmware_set_addr(firmware, fu_struct_fmap_get_base(st_hdr));

	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	if (fu_struct_fmap_get_size(st_hdr) != streamsz) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "file size incorrect, expected 0x%04x got 0x%04x",
			    fu_struct_fmap_get_size(st_hdr),
			    (guint)streamsz);
		return FALSE;
	}
	nareas = fu_struct_fmap_get_nareas(st_hdr);
	if (nareas < 1) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "number of areas invalid");
		return FALSE;
	}
	offset += st_hdr->len;
	for (gsize i = 0; i < nareas; i++) {
		guint32 area_offset;
		guint32 area_size;
		g_autofree gchar *area_name = NULL;
		g_autoptr(FuFirmware) img = fu_firmware_new();
		g_autoptr(GByteArray) st_area = NULL;
		g_autoptr(GInputStream) img_stream = NULL;

		/* load area */
		st_area = fu_struct_fmap_area_parse_stream(stream, offset, error);
		if (st_area == NULL)
			return FALSE;
		area_size = fu_struct_fmap_area_get_size(st_area);
		if (area_size == 0)
			continue;
		area_offset = fu_struct_fmap_area_get_offset(st_area);
		img_stream = fu_partial_input_stream_new(stream,
							 (gsize)area_offset,
							 (gsize)area_size,
							 error);
		if (img_stream == NULL) {
			g_prefix_error_literal(error, "failed to cut FMAP area: ");
			return FALSE;
		}
		if (!fu_firmware_parse_stream(img, img_stream, 0x0, flags, error))
			return FALSE;
		area_name = fu_struct_fmap_area_get_name(st_area);
		fu_firmware_set_id(img, area_name);
		fu_firmware_set_idx(img, i + 1);
		fu_firmware_set_addr(img, area_offset);
		if (!fu_firmware_add_image_full(firmware, img, error))
			return FALSE;

		if (g_strcmp0(area_name, FMAP_AREANAME) == 0) {
			g_autofree gchar *version = NULL;
			version = g_strdup_printf("%d.%d",
						  fu_struct_fmap_get_ver_major(st_hdr),
						  fu_struct_fmap_get_ver_minor(st_hdr));
			fu_firmware_set_version(img, version);
		}
		offset += st_area->len;
	}

	/* success */
	return TRUE;
}

static GByteArray *
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
		g_autoptr(GBytes) fw = fu_firmware_get_bytes_with_patches(img, error);
		if (fw == NULL)
			return NULL;
		fu_byte_array_append_bytes(buf, fw);
	}

	/* success */
	return g_steal_pointer(&buf);
}

static void
fu_fmap_firmware_init(FuFmapFirmware *self)
{
	fu_firmware_set_images_max(FU_FIRMWARE(self), 1024);
}

static void
fu_fmap_firmware_class_init(FuFmapFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_fmap_firmware_parse;
	firmware_class->write = fu_fmap_firmware_write;
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
