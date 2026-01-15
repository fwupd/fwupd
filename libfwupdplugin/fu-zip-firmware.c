/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuZipFirmware"

#include "config.h"

#include "fu-byte-array.h"
#include "fu-common.h"
#include "fu-partial-input-stream.h"
#include "fu-string.h"
#include "fu-zip-firmware.h"
#include "fu-zip-struct.h"

typedef struct {
	gboolean compressed;
} FuZipFirmwarePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuZipFirmware, fu_zip_firmware, FU_TYPE_FIRMWARE)
#define GET_PRIVATE(o) (fu_zip_firmware_get_instance_private(o))

/**
 * fu_zip_firmware_get_compressed:
 * @self: a #FuZipFirmware
 *
 * Gets if the zipinet archive should be compressed.
 *
 * //FIXME: this should be moved to FuZipFile
 *
 * Returns: boolean
 *
 * Since: 2.1.1
 **/
gboolean
fu_zip_firmware_get_compressed(FuZipFirmware *self)
{
	FuZipFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_ZIP_FIRMWARE(self), FALSE);
	return priv->compressed;
}

/**
 * fu_zip_firmware_set_compressed:
 * @self: a #FuZipFirmware
 * @compressed: boolean
 *
 * Sets if the zipinet archive should be compressed.
 *
 * Since: 2.1.1
 **/
void
fu_zip_firmware_set_compressed(FuZipFirmware *self, gboolean compressed)
{
	FuZipFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_ZIP_FIRMWARE(self));
	priv->compressed = compressed;
}

static gboolean
fu_zip_firmware_parse_lfh(FuZipFirmware *self,
			  GInputStream *stream,
			  FuStructZipCdfh *st_cdfh,
			  FuFirmwareParseFlags flags,
			  GError **error)
{
	FuZipCompression compression;
	gsize offset = fu_struct_zip_cdfh_get_offset_lfh(st_cdfh);
	guint32 compressed_size;
	g_autofree gchar *filename = NULL;
	g_autoptr(FuFirmware) img = fu_firmware_new();
	g_autoptr(FuStructZipLfh) st_lfh = NULL;
	g_autoptr(GInputStream) stream_compressed = NULL;
	g_autoptr(GInputStream) stream_raw = NULL;

	/* read local file header */
	fu_firmware_set_offset(img, offset);
	st_lfh = fu_struct_zip_lfh_parse_stream(stream, offset, error);
	if (st_lfh == NULL)
		return FALSE;
	offset += FU_STRUCT_ZIP_LFH_SIZE;

	/* read filename */
	filename = fu_input_stream_read_string(stream,
					       offset,
					       fu_struct_zip_lfh_get_filename_size(st_lfh),
					       error);
	if (filename == NULL) {
		g_prefix_error_literal(error, "failed to read filename: ");
		return FALSE;
	}
	g_debug("filename = %s", filename);
	offset += fu_struct_zip_lfh_get_filename_size(st_lfh);
	offset += fu_struct_zip_lfh_get_extra_size(st_lfh);

	/* read data */
	compressed_size = fu_struct_zip_lfh_get_compressed_size(st_lfh);
	if (compressed_size == 0x0)
		compressed_size = fu_struct_zip_cdfh_get_compressed_size(st_cdfh);
	stream_compressed = fu_partial_input_stream_new(stream, offset, compressed_size, error);
	if (stream_compressed == NULL)
		return FALSE;
	compression = fu_struct_zip_lfh_get_compression(st_lfh);
	if (compression == FU_ZIP_COMPRESSION_NONE) {
		stream_raw = g_object_ref(stream_compressed);
	} else if (compression == FU_ZIP_COMPRESSION_DEFLATE) {
		g_autoptr(GBytes) blob_raw = NULL;
		g_autoptr(GConverter) conv = NULL;
		g_autoptr(GInputStream) stream_deflate = NULL;

		conv = G_CONVERTER(g_zlib_decompressor_new(G_ZLIB_COMPRESSOR_FORMAT_RAW));
		if (!g_seekable_seek(G_SEEKABLE(stream_compressed), 0, G_SEEK_SET, NULL, error))
			return FALSE;
		stream_deflate = g_converter_input_stream_new(stream_compressed, conv);
		g_filter_input_stream_set_close_base_stream(G_FILTER_INPUT_STREAM(stream_deflate),
							    FALSE);
		blob_raw = fu_input_stream_read_bytes(stream_deflate, 0, G_MAXSIZE, NULL, error);
		if (blob_raw == NULL) {
			g_prefix_error_literal(error, "failed to read compressed stream: ");
			return FALSE;
		}
		stream_raw = g_memory_input_stream_new_from_bytes(blob_raw);
	} else {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "%s compression not supported",
			    fu_zip_compression_to_string(compression));
		return FALSE;
	}

	/* verify checksum */
	if ((flags & FU_FIRMWARE_PARSE_FLAG_IGNORE_CHECKSUM) == 0) {
		guint32 crc = 0xFFFFFFFF;
		guint32 uncompressed_crc = fu_struct_zip_lfh_get_uncompressed_crc(st_lfh);

		if (uncompressed_crc == 0x0)
			uncompressed_crc = fu_struct_zip_cdfh_get_uncompressed_crc(st_cdfh);
		if (!fu_input_stream_compute_crc32(stream_raw,
						   FU_CRC_KIND_B32_STANDARD,
						   &crc,
						   error))
			return FALSE;
		if (crc != uncompressed_crc) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "%s CRC 0x%08x invalid, expected 0x%08x",
				    filename,
				    uncompressed_crc,
				    crc);
			return FALSE;
		}
	}

	/* add stream as a image */
	fu_firmware_add_flag(img, FU_FIRMWARE_FLAG_HAS_CHECKSUM);
	fu_firmware_add_flag(img, FU_FIRMWARE_FLAG_HAS_STORED_SIZE);
	fu_firmware_set_id(img, filename);
	if (!fu_firmware_set_stream(img, stream_raw, error))
		return FALSE;
	if (!fu_firmware_add_image(FU_FIRMWARE(self), img, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_zip_firmware_parse(FuFirmware *firmware,
		      GInputStream *stream,
		      FuFirmwareParseFlags flags,
		      GError **error)
{
	FuZipFirmware *self = FU_ZIP_FIRMWARE(firmware);
	gsize streamsz = 0;
	gsize offset = 0;
	g_autoptr(FuStructZipEocd) st_eocd = NULL;

	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;

	/* look for the end of central directory record signature in the last 4K */
	if (streamsz > 0x1000)
		offset = streamsz - 0x1000;
	if (!fu_input_stream_find(stream,
				  (const guint8 *)FU_STRUCT_ZIP_EOCD_DEFAULT_MAGIC,
				  FU_STRUCT_ZIP_EOCD_N_ELEMENTS_MAGIC,
				  offset,
				  &offset,
				  error))
		return FALSE;
	g_debug("found ZIP EOCD magic @0x%x", (guint)offset);
	st_eocd = fu_struct_zip_eocd_parse_stream(stream, offset, error);
	if (st_eocd == NULL)
		return FALSE;
	if (fu_struct_zip_eocd_get_disk_number(st_eocd) != 0x0 ||
	    fu_struct_zip_eocd_get_cd_disk(st_eocd) != 0x0 ||
	    fu_struct_zip_eocd_get_cd_number_disk(st_eocd) !=
		fu_struct_zip_eocd_get_cd_number(st_eocd)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "multiple disk archives not supported");
		return FALSE;
	}

	/* parse central directory file header */
	offset = fu_struct_zip_eocd_get_cd_offset(st_eocd);
	for (guint i = 0; i < fu_struct_zip_eocd_get_cd_number(st_eocd); i++) {
		g_autoptr(FuStructZipCdfh) st_cdfh = NULL;

		/* although the filename is available in the CDFH, trust the one in the LFH */
		st_cdfh = fu_struct_zip_cdfh_parse_stream(stream, offset, error);
		if (st_cdfh == NULL)
			return FALSE;
		if (!fu_zip_firmware_parse_lfh(self, stream, st_cdfh, flags, error))
			return FALSE;
		offset += FU_STRUCT_ZIP_CDFH_SIZE;
		offset += fu_struct_zip_cdfh_get_filename_size(st_cdfh);
		offset += fu_struct_zip_cdfh_get_extra_size(st_cdfh);
		offset += fu_struct_zip_cdfh_get_comment_size(st_cdfh);
	}

	/* success */
	return TRUE;
}

typedef struct {
	guint32 uncompressed_crc;
	guint32 uncompressed_size;
	guint32 compressed_size;
} FuZipFirmwareWriteItem;

static GByteArray *
fu_zip_firmware_write(FuFirmware *firmware, GError **error)
{
	FuZipFirmware *self = FU_ZIP_FIRMWARE(firmware);
	FuZipFirmwarePrivate *priv = GET_PRIVATE(self);
	gsize cd_offset;
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GPtrArray) imgs = fu_firmware_get_images(firmware);
	g_autoptr(FuStructZipEocd) st_eocd = fu_struct_zip_eocd_new();
	g_autofree FuZipFirmwareWriteItem *items = NULL;

	/* stored twice, so avoid computing */
	items = g_new0(FuZipFirmwareWriteItem, imgs->len);

	/* LFHs */
	for (guint i = 0; i < imgs->len; i++) {
		FuFirmware *img = g_ptr_array_index(imgs, i);
		const gchar *filename = fu_firmware_get_id(img);
		g_autoptr(FuStructZipLfh) st_lfh = fu_struct_zip_lfh_new();
		g_autoptr(GBytes) blob = NULL;
		g_autoptr(GBytes) blob_compressed = NULL;

		/* check valid */
		if (filename == NULL) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "filename not provided");
			return NULL;
		}

		/* save for later */
		fu_firmware_set_offset(img, buf->len);

		blob = fu_firmware_get_bytes(img, error);
		if (blob == NULL)
			return NULL;

		if (priv->compressed) {
			g_autoptr(GConverter) conv = NULL;
			g_autoptr(GInputStream) istream_raw = NULL;
			g_autoptr(GInputStream) istream_compressed = NULL;
			conv = G_CONVERTER(g_zlib_compressor_new(G_ZLIB_COMPRESSOR_FORMAT_RAW, -1));
			istream_raw = g_memory_input_stream_new_from_bytes(blob);
			istream_compressed = g_converter_input_stream_new(istream_raw, conv);
			blob_compressed = fu_input_stream_read_bytes(istream_compressed,
								     0,
								     G_MAXSIZE,
								     NULL,
								     error);
			if (blob_compressed == NULL) {
				g_prefix_error_literal(error, "failed to read compressed stream: ");
				return NULL;
			}
		} else {
			blob_compressed = g_bytes_ref(blob);
		}

		items[i].uncompressed_crc = fu_crc32_bytes(FU_CRC_KIND_B32_STANDARD, blob);
		items[i].uncompressed_size = g_bytes_get_size(blob);
		fu_struct_zip_lfh_set_uncompressed_crc(st_lfh, items[i].uncompressed_crc);
		fu_struct_zip_lfh_set_uncompressed_size(st_lfh, items[i].uncompressed_size);
		fu_struct_zip_lfh_set_compression(st_lfh,
						  priv->compressed ? FU_ZIP_COMPRESSION_DEFLATE
								   : FU_ZIP_COMPRESSION_NONE);
		items[i].compressed_size = g_bytes_get_size(blob_compressed);
		fu_struct_zip_lfh_set_compressed_size(st_lfh, items[i].compressed_size);
		fu_struct_zip_lfh_set_filename_size(st_lfh, strlen(filename));

		g_byte_array_append(buf, st_lfh->buf->data, st_lfh->buf->len);
		g_byte_array_append(buf, (const guint8 *)filename, strlen(filename));
		fu_byte_array_append_bytes(buf, blob_compressed);
	}

	/* CDFHs */
	cd_offset = buf->len;
	for (guint i = 0; i < imgs->len; i++) {
		FuFirmware *img = g_ptr_array_index(imgs, i);
		const gchar *filename = fu_firmware_get_id(img);
		g_autoptr(FuStructZipCdfh) st_cdfh = fu_struct_zip_cdfh_new();

		fu_struct_zip_cdfh_set_compression(st_cdfh,
						   priv->compressed ? FU_ZIP_COMPRESSION_DEFLATE
								    : FU_ZIP_COMPRESSION_NONE);
		fu_struct_zip_cdfh_set_compressed_size(st_cdfh, items[i].compressed_size);
		fu_struct_zip_cdfh_set_uncompressed_crc(st_cdfh, items[i].uncompressed_crc);
		fu_struct_zip_cdfh_set_uncompressed_size(st_cdfh, items[i].uncompressed_size);
		fu_struct_zip_cdfh_set_filename_size(st_cdfh, strlen(filename));
		fu_struct_zip_cdfh_set_offset_lfh(st_cdfh, fu_firmware_get_offset(img));

		g_byte_array_append(buf, st_cdfh->buf->data, st_cdfh->buf->len);
		g_byte_array_append(buf, (const guint8 *)filename, strlen(filename));
	}

	/* EOCD */
	fu_struct_zip_eocd_set_cd_offset(st_eocd, cd_offset);
	fu_struct_zip_eocd_set_cd_number_disk(st_eocd, imgs->len);
	fu_struct_zip_eocd_set_cd_number(st_eocd, imgs->len);
	fu_struct_zip_eocd_set_cd_size(st_eocd, buf->len - cd_offset);
	g_byte_array_append(buf, st_eocd->buf->data, st_eocd->buf->len);

	/* success */
	return g_steal_pointer(&buf);
}

static gboolean
fu_zip_firmware_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuZipFirmware *self = FU_ZIP_FIRMWARE(firmware);
	FuZipFirmwarePrivate *priv = GET_PRIVATE(self);
	const gchar *tmp;

	/* simple properties */
	/* FIXME this should be a FuZipCompression */
	tmp = xb_node_query_text(n, "compressed", NULL);
	if (tmp != NULL) {
		if (!fu_strtobool(tmp, &priv->compressed, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_zip_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuZipFirmware *self = FU_ZIP_FIRMWARE(firmware);
	FuZipFirmwarePrivate *priv = GET_PRIVATE(self);
	fu_xmlb_builder_insert_kb(bn, "compressed", priv->compressed);
}

static void
fu_zip_firmware_add_magic(FuFirmware *firmware)
{
	fu_firmware_add_magic(firmware,
			      (const guint8 *)FU_STRUCT_ZIP_LFH_DEFAULT_MAGIC,
			      strlen(FU_STRUCT_ZIP_LFH_DEFAULT_MAGIC),
			      0x0);
}

static void
fu_zip_firmware_class_init(FuZipFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_zip_firmware_parse;
	firmware_class->write = fu_zip_firmware_write;
	firmware_class->build = fu_zip_firmware_build;
	firmware_class->export = fu_zip_firmware_export;
	firmware_class->add_magic = fu_zip_firmware_add_magic;
}

static void
fu_zip_firmware_init(FuZipFirmware *self)
{
	fu_firmware_add_image_gtype(FU_FIRMWARE(self), FU_TYPE_FIRMWARE);
	fu_firmware_set_images_max(FU_FIRMWARE(self), 1024);
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_STORED_SIZE);
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_DEDUPE_ID);
	fu_firmware_set_images_max(FU_FIRMWARE(self), G_MAXUINT16);
}

/**
 * fu_zip_firmware_new:
 *
 * Returns: (transfer full): a #FuZipFirmware
 *
 * Since: 2.1.1
 **/
FuZipFirmware *
fu_zip_firmware_new(void)
{
	return g_object_new(FU_TYPE_ZIP_FIRMWARE, NULL);
}
