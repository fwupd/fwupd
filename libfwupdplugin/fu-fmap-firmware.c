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
#include "fu-mem.h"
#include "fu-memory-input-stream.h"
#include "fu-partial-input-stream.h"
#include "fu-string.h"
#include "fu-uswid-firmware.h"

/**
 * FuFmapFirmware:
 *
 * A FMAP firmware image.
 *
 * NOTE: the `__FMAP__` header may point to sections lower than the stream offset.
 *
 * See also: [class@FuFirmware]
 */

typedef struct {
	gsize signature_offset; /* only for constructing the image */
	guint8 ver_major;
	guint8 ver_minor;
} FuFmapFirmwarePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuFmapFirmware, fu_fmap_firmware, FU_TYPE_FIRMWARE)
#define GET_PRIVATE(o) (fu_fmap_firmware_get_instance_private(o))

#define FU_FMAP_FIRMWARE_AREAS_MAX	   1024
#define FU_FMAP_FIRMWARE_CANDIDATES_MAX	   64
#define FU_FMAP_FIRMWARE_SEARCH_BLOCK_SIZE (64 * FU_KB)

static gboolean
fu_fmap_firmware_name_is_valid(const guint8 *name, gsize namesz)
{
	for (gsize i = 0; i < namesz; i++) {
		if (name[i] == '\0')
			return TRUE;
		if (!g_ascii_isgraph(name[i]))
			return FALSE;
	}
	return FALSE;
}

static GByteArray *
fu_fmap_firmware_read_exact(FuInputStream *stream, gsize offset, gsize count, GError **error)
{
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(GError) error_local = NULL;

	buf = fu_input_stream_read_byte_array(stream, offset, count, NULL, &error_local);
	if (buf == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_READ,
			    "failed to read 0x%x bytes @0x%x: %s",
			    (guint)count,
			    (guint)offset,
			    error_local != NULL ? error_local->message : "unknown error");
		return NULL;
	}
	if (buf->len != count) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_READ,
			    "short read @0x%x, expected 0x%x bytes and got 0x%x",
			    (guint)offset,
			    (guint)count,
			    buf->len);
		return NULL;
	}
	return g_steal_pointer(&buf);
}

static void
fu_fmap_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuFmapFirmware *self = FU_FMAP_FIRMWARE(firmware);
	FuFmapFirmwarePrivate *priv = GET_PRIVATE(self);
	fu_xmlb_builder_insert_kx(bn, "ver_major", priv->ver_major);
	fu_xmlb_builder_insert_kx(bn, "ver_minor", priv->ver_minor);
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
fu_fmap_firmware_validate(FuFirmware *firmware, FuInputStream *stream, gsize offset, GError **error)
{
	gsize fmap_struct_sz = FU_STRUCT_FMAP_SIZE;
	gsize streamsz = 0;
	guint32 fmap_size;
	guint16 nareas;
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(FuInputStream) fmap_stream = NULL;
	g_autoptr(FuStructFmap) st_hdr = NULL;

	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	if (offset > streamsz || streamsz - offset < FU_STRUCT_FMAP_SIZE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "FMAP header @0x%x exceeds stream size 0x%x",
			    (guint)offset,
			    (guint)streamsz);
		return FALSE;
	}
	buf = fu_fmap_firmware_read_exact(stream, offset, FU_STRUCT_FMAP_SIZE, error);
	if (buf == NULL)
		return FALSE;
	blob = g_bytes_new(buf->data, buf->len);
	fmap_stream = fu_memory_input_stream_new_from_bytes(blob);
	st_hdr = fu_struct_fmap_parse_stream(fmap_stream, 0x0, error);
	if (st_hdr == NULL)
		return FALSE;
	if (fu_struct_fmap_get_ver_major(st_hdr) != FU_STRUCT_FMAP_DEFAULT_VER_MAJOR) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "unsupported FMAP major version 0x%x",
			    fu_struct_fmap_get_ver_major(st_hdr));
		return FALSE;
	}
	if (!fu_fmap_firmware_name_is_valid(st_hdr->buf->data + FU_STRUCT_FMAP_OFFSET_NAME,
					    FU_STRUCT_FMAP_SIZE_NAME)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "FMAP name is invalid");
		return FALSE;
	}

	nareas = fu_struct_fmap_get_nareas(st_hdr);
	if (nareas < 1 || nareas > FU_FMAP_FIRMWARE_AREAS_MAX) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "number of FMAP areas invalid: %u",
			    (guint)nareas);
		return FALSE;
	}
	if (!fu_size_checked_inc_product(&fmap_struct_sz, FU_STRUCT_FMAP_AREA_SIZE, nareas, error))
		return FALSE;

	fmap_size = fu_struct_fmap_get_size(st_hdr);
	if (fmap_size < fmap_struct_sz) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "FMAP image size 0x%x is smaller than the map structure 0x%x",
			    fmap_size,
			    (guint)fmap_struct_sz);
		return FALSE;
	}
	if (fmap_size > streamsz || offset > streamsz - fmap_struct_sz) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "FMAP exceeds stream size 0x%x",
			    (guint)streamsz);
		return FALSE;
	}
	if (offset > fmap_size - fmap_struct_sz) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "FMAP structure @0x%x exceeds image size 0x%x",
			    (guint)offset,
			    fmap_size);
		return FALSE;
	}

	/* read the table once so validating each area cannot amplify stream reads */
	g_clear_pointer(&buf, g_byte_array_unref);
	g_clear_pointer(&blob, g_bytes_unref);
	g_clear_object(&fmap_stream);
	buf = fu_fmap_firmware_read_exact(stream, offset, fmap_struct_sz, error);
	if (buf == NULL)
		return FALSE;
	blob = g_bytes_new(buf->data, buf->len);
	fmap_stream = fu_memory_input_stream_new_from_bytes(blob);
	offset = st_hdr->buf->len;
	for (guint16 i = 0; i < nareas; i++) {
		guint32 area_offset;
		guint32 area_size;
		g_autoptr(FuStructFmapArea) st_area = NULL;

		st_area = fu_struct_fmap_area_parse_stream(fmap_stream, offset, error);
		if (st_area == NULL)
			return FALSE;
		if (!fu_fmap_firmware_name_is_valid(st_area->buf->data +
							FU_STRUCT_FMAP_AREA_OFFSET_NAME,
						    FU_STRUCT_FMAP_AREA_SIZE_NAME)) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "FMAP area 0x%x name is invalid",
				    (guint)i);
			return FALSE;
		}
		area_offset = fu_struct_fmap_area_get_offset(st_area);
		area_size = fu_struct_fmap_area_get_size(st_area);
		if (area_offset > fmap_size || area_size > fmap_size - area_offset) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "FMAP area 0x%x exceeds image size 0x%x",
				    (guint)i,
				    fmap_size);
			return FALSE;
		}
		if (!fu_size_checked_inc(&offset, st_area->buf->len, error))
			return FALSE;
	}
	return TRUE;
}

static gboolean
fu_fmap_firmware_validate_image_size(FuFmapFirmware *self,
				     FuInputStream *stream,
				     gsize offset,
				     gsize image_size,
				     GError **error)
{
	guint32 fmap_size = 0;
	g_autoptr(GError) error_local = NULL;

	if (!fu_fmap_firmware_validate(FU_FIRMWARE(self), stream, offset, error))
		return FALSE;
	if (image_size == 0)
		return TRUE;
	if (!fu_input_stream_read_u32(stream,
				      offset + FU_STRUCT_FMAP_OFFSET_SIZE,
				      &fmap_size,
				      G_LITTLE_ENDIAN,
				      &error_local)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_READ,
			    "failed to read FMAP image size @0x%x: %s",
			    (guint)(offset + FU_STRUCT_FMAP_OFFSET_SIZE),
			    error_local != NULL ? error_local->message : "unknown error");
		return FALSE;
	}
	if (fmap_size != image_size) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "FMAP image size 0x%x does not match expected size 0x%x",
			    fmap_size,
			    (guint)image_size);
		return FALSE;
	}
	return TRUE;
}

/**
 * fu_fmap_firmware_find:
 * @self: a #FuFmapFirmware
 * @stream: firmware image stream
 * @offset: offset to start searching
 * @search_size: size of the range to search
 * @image_size: expected FMAP image size, or 0 to accept any size
 * @offset_found: (out): offset of the validated FMAP
 * @error: (nullable): optional return location for an error
 *
 * Finds exactly one structurally valid FMAP in @stream. Unreadable ranges and
 * excessive candidate signatures are rejected, as are multiple matching maps.
 *
 * Returns: %TRUE if a valid FMAP was found
 *
 * Since: 2.2.2
 **/
gboolean
fu_fmap_firmware_find(FuFmapFirmware *self,
		      FuInputStream *stream,
		      gsize offset,
		      gsize search_size,
		      gsize image_size,
		      gsize *offset_found,
		      GError **error)
{
	gsize offset_match = G_MAXSIZE;
	gsize search_end;
	gsize streamsz = 0;
	guint candidates_cnt = 0;
	guint8 overlap[FU_STRUCT_FMAP_SIZE_SIGNATURE - 1] = {0x0};
	gsize overlap_sz = 0;

	g_return_val_if_fail(FU_IS_FMAP_FIRMWARE(self), FALSE);
	g_return_val_if_fail(FU_IS_INPUT_STREAM(stream), FALSE);
	g_return_val_if_fail(offset_found != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	if (offset > streamsz || search_size > streamsz - offset ||
	    search_size < FU_STRUCT_FMAP_SIZE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "FMAP search range 0x%x+0x%x is invalid for stream size 0x%x",
			    (guint)offset,
			    (guint)search_size,
			    (guint)streamsz);
		return FALSE;
	}
	search_end = offset + search_size;
	/* preserve enough overlap to find a signature that crosses a block boundary */
	for (gsize block_offset = offset; block_offset < search_end;) {
		gsize blocksz = MIN(FU_FMAP_FIRMWARE_SEARCH_BLOCK_SIZE, search_end - block_offset);
		gsize search_offset = 0;
		g_autoptr(GByteArray) buf = NULL;
		g_autoptr(GByteArray) buf_search = g_byte_array_new();
		g_autoptr(GError) error_local = NULL;

		buf = fu_fmap_firmware_read_exact(stream, block_offset, blocksz, &error_local);
		if (buf == NULL) {
			g_propagate_prefixed_error(error,
						   g_steal_pointer(&error_local),
						   "failed to read FMAP search block @0x%x: ",
						   (guint)block_offset);
			return FALSE;
		}
		if (overlap_sz > 0)
			g_byte_array_append(buf_search, overlap, overlap_sz);
		g_byte_array_append(buf_search, buf->data, buf->len);

		while (search_offset + FU_STRUCT_FMAP_SIZE_SIGNATURE <= buf_search->len) {
			gsize offset_tmp = 0;
			gsize offset_fmap;
			g_autoptr(GError) error_fmap = NULL;

			if (!fu_memmem_safe(buf_search->data + search_offset,
					    buf_search->len - search_offset,
					    (const guint8 *)FU_STRUCT_FMAP_DEFAULT_SIGNATURE,
					    FU_STRUCT_FMAP_SIZE_SIGNATURE,
					    &offset_tmp,
					    NULL))
				break;
			offset_tmp += search_offset;
			search_offset = offset_tmp + 1;
			offset_fmap = block_offset - overlap_sz + offset_tmp;
			if (++candidates_cnt > FU_FMAP_FIRMWARE_CANDIDATES_MAX) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_FILE,
					    "too many FMAP candidates, maximum is %u",
					    (guint)FU_FMAP_FIRMWARE_CANDIDATES_MAX);
				return FALSE;
			}
			if (!fu_fmap_firmware_validate_image_size(self,
								  stream,
								  offset_fmap,
								  image_size,
								  &error_fmap)) {
				if (g_error_matches(error_fmap, FWUPD_ERROR, FWUPD_ERROR_READ)) {
					g_propagate_prefixed_error(
					    error,
					    g_steal_pointer(&error_fmap),
					    "failed to validate FMAP candidate @0x%x: ",
					    (guint)offset_fmap);
					return FALSE;
				}
				continue;
			}
			if (offset_match != G_MAXSIZE) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_FILE,
					    "multiple valid FMAPs found at 0x%x and 0x%x",
					    (guint)offset_match,
					    (guint)offset_fmap);
				return FALSE;
			}
			offset_match = offset_fmap;
		}

		overlap_sz = MIN(sizeof(overlap), buf_search->len);
		if (!fu_memcpy_safe(overlap,
				    sizeof(overlap),
				    0x0,
				    buf_search->data,
				    buf_search->len,
				    buf_search->len - overlap_sz,
				    overlap_sz,
				    error))
			return FALSE;
		block_offset += blocksz;
	}

	if (offset_match == G_MAXSIZE) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "failed to find a valid FMAP");
		return FALSE;
	}
	*offset_found = offset_match;
	return TRUE;
}

static gboolean
fu_fmap_firmware_parse(FuFirmware *firmware,
		       FuInputStream *stream,
		       gsize offset,
		       FuFirmwareParseFlags flags,
		       GError **error)
{
	FuFmapFirmware *self = FU_FMAP_FIRMWARE(firmware);
	FuFmapFirmwarePrivate *priv = GET_PRIVATE(self);
	gsize streamsz = 0;
	guint32 nareas;
	g_autoptr(FuStructFmap) st_hdr = NULL;

	/* parse */
	st_hdr = fu_struct_fmap_parse_stream(stream, offset, error);
	if (st_hdr == NULL)
		return FALSE;
	fu_firmware_set_addr(firmware, fu_struct_fmap_get_base(st_hdr));
	fu_firmware_set_size(firmware, fu_struct_fmap_get_size(st_hdr));
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
	if (nareas > FU_FMAP_FIRMWARE_AREAS_MAX) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "excessive number of areas: %u",
			    (guint)nareas);
		return FALSE;
	}
	if (!fu_size_checked_inc(&offset, st_hdr->buf->len, error)) {
		g_prefix_error_literal(error, "FMAP header offset overflow: ");
		return FALSE;
	}

	for (gsize i = 0; i < nareas; i++) {
		guint32 area_offset;
		guint32 area_size;
		g_autofree gchar *area_name = NULL;
		g_autoptr(FuFirmware) img = NULL;
		g_autoptr(FuStructFmapArea) st_area = NULL;
		g_autoptr(FuInputStream) img_stream = NULL;

		/* load area */
		st_area = fu_struct_fmap_area_parse_stream(stream, offset, error);
		if (st_area == NULL)
			return FALSE;
		if (!fu_size_checked_inc(&offset, st_area->buf->len, error)) {
			g_prefix_error(error, "FMAP area 0x%x offset overflow: ", (guint)i);
			return FALSE;
		}
		area_size = fu_struct_fmap_area_get_size(st_area);
		if (area_size == 0)
			continue;

		/* this is an absolute stream, referencing from the start of the base stream */
		area_offset = fu_struct_fmap_area_get_offset(st_area);
		img_stream = fu_partial_input_stream_new(stream,
							 (gsize)area_offset,
							 (gsize)area_size,
							 error);
		if (img_stream == NULL) {
			g_prefix_error_literal(error, "failed to cut FMAP area: ");
			return FALSE;
		}
		area_name = fu_struct_fmap_area_get_name(st_area);
		if (g_strcmp0(area_name, "SBOM") == 0) {
			img = fu_firmware_new_from_gtypes(img_stream,
							  0x0,
							  flags,
							  error,
							  FU_TYPE_USWID_FIRMWARE,
							  FU_TYPE_FIRMWARE,
							  G_TYPE_INVALID);
			if (img == NULL)
				return FALSE;
		} else {
			img = fu_firmware_new();
			if (!fu_firmware_parse_stream(img, img_stream, 0x0, flags, error))
				return FALSE;
		}
		fu_firmware_set_id(img, area_name);
		fu_firmware_set_idx(img, i + 1);
		fu_firmware_set_addr(img, area_offset);
		fu_firmware_set_size(img, area_size);
		if (!fu_firmware_add_image(firmware, img, error))
			return FALSE;
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
	g_autoptr(GPtrArray) images = fu_firmware_get_images(firmware);
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(FuStructFmap) st_hdr = fu_struct_fmap_new();

	/* pad to offset */
	fu_byte_array_set_size(buf, priv->signature_offset, 0x00);

	/* write each image if not already a blob */
	for (guint i = 0; i < images->len; i++) {
		FuFirmware *img = g_ptr_array_index(images, i);
		g_autoptr(GBytes) fw = fu_firmware_get_bytes(img, NULL);
		if (fw != NULL)
			continue;
		fw = fu_firmware_write(img, error);
		if (fw == NULL)
			return NULL;
		fu_firmware_set_bytes(img, fw);
	}

	/* add header */
	total_sz = st_hdr->buf->len;
	if (!fu_size_checked_inc_product(&total_sz, FU_STRUCT_FMAP_AREA_SIZE, images->len, error))
		return NULL;
	offset = total_sz;
	for (guint i = 0; i < images->len; i++) {
		FuFirmware *img = g_ptr_array_index(images, i);
		g_autoptr(GBytes) fw = fu_firmware_get_bytes(img, NULL);
		if (fw == NULL)
			continue;
		if (!fu_size_checked_inc(&total_sz, g_bytes_get_size(fw), error))
			return NULL;
	}

	/* header */
	if (!fu_size_checked_inc(&total_sz, priv->signature_offset, error))
		return NULL;
	fu_struct_fmap_set_ver_major(st_hdr, priv->ver_major);
	fu_struct_fmap_set_ver_minor(st_hdr, priv->ver_minor);
	fu_struct_fmap_set_base(st_hdr, fu_firmware_get_addr(firmware));
	fu_struct_fmap_set_nareas(st_hdr, images->len);
	fu_struct_fmap_set_size(st_hdr, total_sz);
	fu_byte_array_append_array(buf, st_hdr->buf);

	/* add each area */
	for (guint i = 0; i < images->len; i++) {
		FuFirmware *img = g_ptr_array_index(images, i);
		g_autoptr(GBytes) fw = fu_firmware_get_bytes(img, NULL);
		g_autoptr(FuStructFmapArea) st_area = fu_struct_fmap_area_new();
		if (fw == NULL)
			continue;
		fu_struct_fmap_area_set_offset(st_area, priv->signature_offset + offset);
		fu_struct_fmap_area_set_size(st_area, g_bytes_get_size(fw));
		if (fu_firmware_get_id(img) != NULL) {
			if (!fu_struct_fmap_area_set_name(st_area, fu_firmware_get_id(img), error))
				return NULL;
		}
		fu_byte_array_append_array(buf, st_area->buf);
		if (!fu_size_checked_inc(&offset, g_bytes_get_size(fw), error))
			return NULL;
	}

	/* add the images */
	for (guint i = 0; i < images->len; i++) {
		FuFirmware *img = g_ptr_array_index(images, i);
		g_autoptr(GBytes) fw = fu_firmware_get_bytes(img, error);
		if (fw == NULL)
			return NULL;
		fu_byte_array_append_bytes(buf, fw);
	}

	/* success */
	return g_steal_pointer(&buf);
}

static void
fu_fmap_firmware_add_magic(FuFirmware *firmware)
{
	fu_firmware_add_magic(firmware,
			      (const guint8 *)FU_STRUCT_FMAP_DEFAULT_SIGNATURE,
			      FU_STRUCT_FMAP_SIZE_SIGNATURE,
			      0x0);
}

static void
fu_fmap_firmware_init(FuFmapFirmware *self)
{
	FuFmapFirmwarePrivate *priv = GET_PRIVATE(self);
	priv->ver_major = FU_STRUCT_FMAP_DEFAULT_VER_MAJOR;
	priv->ver_minor = FU_STRUCT_FMAP_DEFAULT_VER_MINOR;
}

static void
fu_fmap_firmware_class_init(FuFmapFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	fu_firmware_add_image_gtype(firmware_class, FU_TYPE_USWID_FIRMWARE);
	fu_firmware_add_image_gtype(firmware_class, FU_TYPE_FIRMWARE);
	fu_firmware_set_size_max(firmware_class, 256 * FU_MB);
	firmware_class->parse_full = fu_fmap_firmware_parse;
	firmware_class->validate = fu_fmap_firmware_validate;
	firmware_class->write = fu_fmap_firmware_write;
	firmware_class->export = fu_fmap_firmware_export;
	firmware_class->build = fu_fmap_firmware_build;
	firmware_class->add_magic = fu_fmap_firmware_add_magic;
	fu_firmware_set_images_max(firmware_class, 1024);
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
