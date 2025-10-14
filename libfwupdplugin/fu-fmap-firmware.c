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
#include "fu-string.h"

/**
 * FuFmapFirmware:
 *
 * A FMAP firmware image.
 *
 * See also: [class@FuFirmware]
 */

#define FMAP_AREANAME "FMAP"

typedef struct {
	gsize signature_offset;
	guint8 ver_major;
	guint8 ver_minor;
} FuFmapFirmwarePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuFmapFirmware, fu_fmap_firmware, FU_TYPE_FIRMWARE)
#define GET_PRIVATE(o) (fu_fmap_firmware_get_instance_private(o))

/**
 * fu_fmap_firmware_set_signature_offset:
 * @self: a #FuFmapFirmware
 * @signature_offset: raw offset
 *
 * Sets the signature offset. This is different to the offset returned by fu_firmware_get_offset()
 * which points to the position of the entire image with regards to the parent.
 *
 * The `FLASH` region for example enumerates the entire size of the stream, and the `__FMAP__`
 * header may be positioned in a `FMAP` section *within* the image. This "signature offset" points
 * to the `__FMAP__` header itself.
 *
 * Since: 2.0.17
 **/
void
fu_fmap_firmware_set_signature_offset(FuFmapFirmware *self, gsize signature_offset)
{
	FuFmapFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_FMAP_FIRMWARE(self));
	priv->signature_offset = signature_offset;
}

/**
 * fu_fmap_firmware_get_signature_offset:
 * @self: a #FuFmapFirmware
 *
 * Gets the signature offset.
 *
 * Returns: offset, or %G_MAXSIZE for unset
 *
 * Since: 2.0.17
 **/
gsize
fu_fmap_firmware_get_signature_offset(FuFmapFirmware *self)
{
	FuFmapFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_FMAP_FIRMWARE(self), G_MAXSIZE);
	return priv->signature_offset;
}

static void
fu_fmap_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuFmapFirmware *self = FU_FMAP_FIRMWARE(firmware);
	FuFmapFirmwarePrivate *priv = GET_PRIVATE(self);
	fu_xmlb_builder_insert_kx(bn, "ver_major", priv->ver_major);
	fu_xmlb_builder_insert_kx(bn, "ver_minor", priv->ver_minor);
	if (priv->signature_offset != G_MAXSIZE)
		fu_xmlb_builder_insert_kx(bn, "signature_offset", priv->signature_offset);
}

static gboolean
fu_fmap_firmware_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuFmapFirmware *self = FU_FMAP_FIRMWARE(firmware);
	FuFmapFirmwarePrivate *priv = GET_PRIVATE(self);
	const gchar *tmp;

	/* simple properties */
	tmp = xb_node_query_text(n, "signature_offset", NULL);
	if (tmp != NULL) {
		guint64 tmp64 = 0;
		if (!fu_strtoull(tmp, &tmp64, 0x0, G_MAXSIZE, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		priv->signature_offset = (gsize)tmp64;
	}
	tmp = xb_node_query_text(n, "ver_major", NULL);
	if (tmp != NULL) {
		guint64 tmp64 = 0;
		if (!fu_strtoull(tmp, &tmp64, 0x0, G_MAXUINT8, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		priv->ver_major = (gsize)tmp64;
	}
	tmp = xb_node_query_text(n, "ver_minor", NULL);
	if (tmp != NULL) {
		guint64 tmp64 = 0;
		if (!fu_strtoull(tmp, &tmp64, 0x0, G_MAXUINT8, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		priv->ver_minor = (gsize)tmp64;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_fmap_firmware_parse(FuFirmware *firmware,
		       GInputStream *stream,
		       FuFirmwareParseFlags flags,
		       GError **error)
{
	FuFmapFirmware *self = FU_FMAP_FIRMWARE(firmware);
	FuFmapFirmwarePrivate *priv = GET_PRIVATE(self);
	gsize offset;
	gsize streamsz = 0;
	guint32 nareas;
	g_autoptr(FuStructFmap) st_hdr = NULL;

	/* find the magic token if not already specified */
	if (priv->signature_offset == G_MAXSIZE) {
		if (!fu_input_stream_find(stream,
					  (const guint8 *)FU_STRUCT_FMAP_DEFAULT_SIGNATURE,
					  FU_STRUCT_FMAP_SIZE_SIGNATURE,
					  &priv->signature_offset,
					  error))
			return FALSE;
	}

	/* parse */
	st_hdr = fu_struct_fmap_parse_stream(stream, priv->signature_offset, error);
	if (st_hdr == NULL)
		return FALSE;
	fu_firmware_set_addr(firmware, fu_struct_fmap_get_base(st_hdr));
	priv->ver_major = fu_struct_fmap_get_ver_major(st_hdr);
	priv->ver_minor = fu_struct_fmap_get_ver_minor(st_hdr);

	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	if (fu_struct_fmap_get_size(st_hdr) > streamsz) {
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
	offset = priv->signature_offset + st_hdr->buf->len;
	for (gsize i = 0; i < nareas; i++) {
		guint32 area_offset;
		guint32 area_size;
		g_autofree gchar *area_name = NULL;
		g_autoptr(FuFirmware) img = fu_firmware_new();
		g_autoptr(FuStructFmapArea) st_area = NULL;
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
		if (!fu_firmware_add_image(firmware, img, error))
			return FALSE;
		offset += st_area->buf->len;
	}

	/* success */
	return TRUE;
}

static GByteArray *
fu_fmap_firmware_write(FuFirmware *firmware, GError **error)
{
	FuFmapFirmware *self = FU_FMAP_FIRMWARE(firmware);
	FuFmapFirmwarePrivate *priv = GET_PRIVATE(self);
	gsize total_sz;
	gsize offset;
	gsize signature_offset = 0;
	g_autoptr(GPtrArray) images = fu_firmware_get_images(firmware);
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(FuStructFmap) st_hdr = fu_struct_fmap_new();

	/* pad to offset */
	if (priv->signature_offset > 0 && priv->signature_offset != G_MAXSIZE)
		signature_offset = priv->signature_offset;
	fu_byte_array_set_size(buf, signature_offset, 0x00);

	/* add header */
	total_sz = offset = st_hdr->buf->len + (FU_STRUCT_FMAP_AREA_SIZE * images->len);
	for (guint i = 0; i < images->len; i++) {
		FuFirmware *img = g_ptr_array_index(images, i);
		g_autoptr(GBytes) fw = fu_firmware_get_bytes_with_patches(img, error);
		if (fw == NULL)
			return NULL;
		total_sz += g_bytes_get_size(fw);
	}

	/* header */
	fu_struct_fmap_set_ver_major(st_hdr, priv->ver_major);
	fu_struct_fmap_set_ver_minor(st_hdr, priv->ver_minor);
	fu_struct_fmap_set_base(st_hdr, fu_firmware_get_addr(firmware));
	fu_struct_fmap_set_nareas(st_hdr, images->len);
	fu_struct_fmap_set_size(st_hdr, signature_offset + total_sz);
	fu_byte_array_append_array(buf, st_hdr->buf);

	/* add each area */
	for (guint i = 0; i < images->len; i++) {
		FuFirmware *img = g_ptr_array_index(images, i);
		g_autoptr(GBytes) fw = fu_firmware_get_bytes_with_patches(img, NULL);
		g_autoptr(FuStructFmapArea) st_area = fu_struct_fmap_area_new();
		fu_struct_fmap_area_set_offset(st_area, signature_offset + offset);
		fu_struct_fmap_area_set_size(st_area, g_bytes_get_size(fw));
		if (fu_firmware_get_id(img) != NULL) {
			if (!fu_struct_fmap_area_set_name(st_area, fu_firmware_get_id(img), error))
				return NULL;
		}
		fu_byte_array_append_array(buf, st_area->buf);
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
	FuFmapFirmwarePrivate *priv = GET_PRIVATE(self);
	priv->signature_offset = G_MAXSIZE;
	priv->ver_major = FU_STRUCT_FMAP_DEFAULT_VER_MAJOR;
	priv->ver_minor = FU_STRUCT_FMAP_DEFAULT_VER_MINOR;
	fu_firmware_set_images_max(FU_FIRMWARE(self), 1024);
}

static void
fu_fmap_firmware_class_init(FuFmapFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_fmap_firmware_parse;
	firmware_class->write = fu_fmap_firmware_write;
	firmware_class->export = fu_fmap_firmware_export;
	firmware_class->build = fu_fmap_firmware_build;
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
