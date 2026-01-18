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
#include "fu-zip-file.h"
#include "fu-zip-firmware.h"
#include "fu-zip-struct.h"

G_DEFINE_TYPE(FuZipFirmware, fu_zip_firmware, FU_TYPE_FIRMWARE)

#define FU_ZIP_FIRMWARE_EOCD_OFFSET_MAX 0x4000

static gboolean
fu_zip_firmware_parse_extra(GInputStream *stream, gsize offset, gsize extra_size, GError **error)
{
	for (gsize i = 0; i < extra_size; i += FU_STRUCT_ZIP_EXTRA_HDR_SIZE) {
		g_autoptr(FuStructZipExtraHdr) st_ehdr = NULL;
		st_ehdr = fu_struct_zip_extra_hdr_parse_stream(stream, offset + i, error);
		if (st_ehdr == NULL)
			return FALSE;
		i += fu_struct_zip_extra_hdr_get_datasz(st_ehdr);
	}
	return TRUE;
}

static FuFirmware *
fu_zip_firmware_parse_lfh(FuZipFirmware *self,
			  GInputStream *stream,
			  FuStructZipCdfh *st_cdfh,
			  FuFirmwareParseFlags flags,
			  GError **error)
{
	FuZipCompression compression;
	gsize offset = fu_struct_zip_cdfh_get_offset_lfh(st_cdfh);
	guint32 actual_crc = 0xFFFFFFFF;
	guint32 compressed_size;
	guint32 uncompressed_size;
	guint32 uncompressed_crc;
	g_autofree gchar *filename = NULL;
	g_autoptr(FuStructZipLfh) st_lfh = NULL;
	g_autoptr(FuFirmware) zip_file = fu_zip_file_new();
	g_autoptr(GInputStream) stream_compressed = NULL;

	/* read local file header */
	fu_firmware_set_offset(zip_file, offset);
	st_lfh = fu_struct_zip_lfh_parse_stream(stream, offset, error);
	if (st_lfh == NULL)
		return NULL;
	offset += FU_STRUCT_ZIP_LFH_SIZE;

	/* read filename */
	filename = fu_input_stream_read_string(stream,
					       offset,
					       fu_struct_zip_lfh_get_filename_size(st_lfh),
					       error);
	if (filename == NULL) {
		g_prefix_error_literal(error, "failed to read filename: ");
		return NULL;
	}
	offset += fu_struct_zip_lfh_get_filename_size(st_lfh);

	/* parse the extra data blob just because we can */
	if (!fu_zip_firmware_parse_extra(stream,
					 offset,
					 fu_struct_zip_lfh_get_extra_size(st_lfh),
					 error))
		return FALSE;

	offset += fu_struct_zip_lfh_get_extra_size(st_lfh);

	/* read crc */
	uncompressed_crc = fu_struct_zip_lfh_get_uncompressed_crc(st_lfh);
	if (uncompressed_crc == 0x0)
		uncompressed_crc = fu_struct_zip_cdfh_get_uncompressed_crc(st_cdfh);

	/* read data */
	compressed_size = fu_struct_zip_lfh_get_compressed_size(st_lfh);
	if (compressed_size == 0x0)
		compressed_size = fu_struct_zip_cdfh_get_compressed_size(st_cdfh);
	if (compressed_size == 0xFFFFFFFF) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "zip64 not supported");
		return NULL;
	}
	uncompressed_size = fu_struct_zip_lfh_get_uncompressed_size(st_lfh);
	if (uncompressed_size == 0x0)
		uncompressed_size = fu_struct_zip_cdfh_get_uncompressed_size(st_cdfh);
	if (uncompressed_size == 0xFFFFFFFF) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "zip64 not supported");
		return NULL;
	}
	stream_compressed = fu_partial_input_stream_new(stream, offset, compressed_size, error);
	if (stream_compressed == NULL)
		return NULL;
	compression = fu_struct_zip_lfh_get_compression(st_lfh);
	if (compression == FU_ZIP_COMPRESSION_NONE) {
		if (compressed_size != uncompressed_size) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "no compression but compressed (0x%x) != uncompressed (0x%x)",
				    (guint)compressed_size,
				    (guint)uncompressed_size);
			return NULL;
		}
		if ((flags & FU_FIRMWARE_PARSE_FLAG_IGNORE_CHECKSUM) == 0) {
			if (!fu_input_stream_compute_crc32(stream_compressed,
							   FU_CRC_KIND_B32_STANDARD,
							   &actual_crc,
							   error))
				return NULL;
		}
		if (!fu_firmware_set_stream(zip_file, stream_compressed, error))
			return NULL;
	} else if (compression == FU_ZIP_COMPRESSION_DEFLATE) {
		g_autoptr(GBytes) blob_raw = NULL;
		g_autoptr(GConverter) conv = NULL;
		g_autoptr(GInputStream) stream_deflate = NULL;

		conv = G_CONVERTER(g_zlib_decompressor_new(G_ZLIB_COMPRESSOR_FORMAT_RAW));
		if (!g_seekable_seek(G_SEEKABLE(stream_compressed), 0, G_SEEK_SET, NULL, error))
			return NULL;
		stream_deflate = g_converter_input_stream_new(stream_compressed, conv);
		g_filter_input_stream_set_close_base_stream(G_FILTER_INPUT_STREAM(stream_deflate),
							    FALSE);
		blob_raw =
		    fu_input_stream_read_bytes(stream_deflate, 0, uncompressed_size, NULL, error);
		if (blob_raw == NULL) {
			g_prefix_error_literal(error, "failed to read compressed stream: ");
			return NULL;
		}
		if (g_bytes_get_size(blob_raw) != uncompressed_size) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "invalid decompression, got 0x%x bytes but expected 0x%x",
				    (guint)g_bytes_get_size(blob_raw),
				    (guint)uncompressed_size);
			return NULL;
		}
		fu_firmware_set_bytes(zip_file, blob_raw);
		if ((flags & FU_FIRMWARE_PARSE_FLAG_IGNORE_CHECKSUM) == 0)
			actual_crc = fu_crc32_bytes(FU_CRC_KIND_B32_STANDARD, blob_raw);
	} else {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "%s compression not supported",
			    fu_zip_compression_to_string(compression));
		return NULL;
	}
	fu_zip_file_set_compression(FU_ZIP_FILE(zip_file), compression);

	/* verify checksum */
	if ((flags & FU_FIRMWARE_PARSE_FLAG_IGNORE_CHECKSUM) == 0) {
		if (actual_crc != uncompressed_crc) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "%s CRC 0x%08x invalid, expected 0x%08x",
				    filename,
				    actual_crc,
				    uncompressed_crc);
			return NULL;
		}
	}

	/* add as a image */
	if (flags & FU_FIRMWARE_PARSE_FLAG_ONLY_BASENAME) {
		g_autofree gchar *filename_basename = g_path_get_basename(filename);
		fu_firmware_set_id(zip_file, filename_basename);
	} else {
		fu_firmware_set_id(zip_file, filename);
	}

	/* success */
	return g_steal_pointer(&zip_file);
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
	if (streamsz > FU_ZIP_FIRMWARE_EOCD_OFFSET_MAX)
		offset = streamsz - FU_ZIP_FIRMWARE_EOCD_OFFSET_MAX;
	if (!fu_input_stream_find(stream,
				  (const guint8 *)FU_STRUCT_ZIP_EOCD_DEFAULT_MAGIC,
				  FU_STRUCT_ZIP_EOCD_N_ELEMENTS_MAGIC,
				  offset,
				  &offset,
				  error)) {
		g_prefix_error_literal(error, "failed to find zip EOCD signature: ");
		return FALSE;
	}
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

	/* archives over 4GB do not make sense here */
	if (fu_struct_zip_eocd_get_cd_size(st_eocd) == 0xFFFFFFFF) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "zip64 not supported");
		return FALSE;
	}

	/* parse central directory file header */
	offset = fu_struct_zip_eocd_get_cd_offset(st_eocd);
	for (guint i = 0; i < fu_struct_zip_eocd_get_cd_number(st_eocd); i++) {
		g_autoptr(FuFirmware) zip_file = NULL;
		g_autoptr(FuStructZipCdfh) st_cdfh = NULL;

		/* although the filename is available in the CDFH, trust the one in the LFH */
		st_cdfh = fu_struct_zip_cdfh_parse_stream(stream, offset, error);
		if (st_cdfh == NULL)
			return FALSE;
		if (fu_struct_zip_cdfh_get_flags(st_cdfh) & FU_ZIP_FLAG_ENCRYPTED) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "encryption not supported");
			return FALSE;
		}
		zip_file = fu_zip_firmware_parse_lfh(self, stream, st_cdfh, flags, error);
		if (zip_file == NULL)
			return FALSE;

		offset += FU_STRUCT_ZIP_CDFH_SIZE;
		offset += fu_struct_zip_cdfh_get_filename_size(st_cdfh);

		/* parse the extra data blob just because we can */
		if (!fu_zip_firmware_parse_extra(stream,
						 offset,
						 fu_struct_zip_cdfh_get_extra_size(st_cdfh),
						 error))
			return FALSE;
		offset += fu_struct_zip_cdfh_get_extra_size(st_cdfh);

		/* ignore the comment */
		offset += fu_struct_zip_cdfh_get_comment_size(st_cdfh);

		/* add image */
		if (!fu_firmware_add_image(FU_FIRMWARE(self), zip_file, error))
			return FALSE;
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
	gsize cd_offset;
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GPtrArray) imgs = fu_firmware_get_images(firmware);
	g_autoptr(FuStructZipEocd) st_eocd = fu_struct_zip_eocd_new();
	g_autofree FuZipFirmwareWriteItem *items = NULL;

	/* stored twice, so avoid computing */
	items = g_new0(FuZipFirmwareWriteItem, imgs->len);

	/* LFHs */
	for (guint i = 0; i < imgs->len; i++) {
		FuZipFile *zip_file = g_ptr_array_index(imgs, i);
		FuZipCompression compression = fu_zip_file_get_compression(zip_file);
		const gchar *filename = fu_firmware_get_id(FU_FIRMWARE(zip_file));
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
		fu_firmware_set_offset(FU_FIRMWARE(zip_file), buf->len);
		blob = fu_firmware_get_bytes(FU_FIRMWARE(zip_file), error);
		if (blob == NULL)
			return NULL;

		if (compression == FU_ZIP_COMPRESSION_NONE) {
			blob_compressed = g_bytes_ref(blob);
		} else if (compression == FU_ZIP_COMPRESSION_DEFLATE) {
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
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "%s compression not supported",
				    fu_zip_compression_to_string(compression));
			return NULL;
		}

		items[i].uncompressed_crc = fu_crc32_bytes(FU_CRC_KIND_B32_STANDARD, blob);
		items[i].uncompressed_size = g_bytes_get_size(blob);
		fu_struct_zip_lfh_set_uncompressed_crc(st_lfh, items[i].uncompressed_crc);
		fu_struct_zip_lfh_set_uncompressed_size(st_lfh, items[i].uncompressed_size);
		fu_struct_zip_lfh_set_compression(st_lfh, compression);
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
		FuZipFile *zip_file = g_ptr_array_index(imgs, i);
		FuZipCompression compression = fu_zip_file_get_compression(zip_file);
		const gchar *filename = fu_firmware_get_id(FU_FIRMWARE(zip_file));
		g_autoptr(FuStructZipCdfh) st_cdfh = fu_struct_zip_cdfh_new();

		fu_struct_zip_cdfh_set_compression(st_cdfh, compression);
		fu_struct_zip_cdfh_set_compressed_size(st_cdfh, items[i].compressed_size);
		fu_struct_zip_cdfh_set_uncompressed_crc(st_cdfh, items[i].uncompressed_crc);
		fu_struct_zip_cdfh_set_uncompressed_size(st_cdfh, items[i].uncompressed_size);
		fu_struct_zip_cdfh_set_filename_size(st_cdfh, strlen(filename));
		fu_struct_zip_cdfh_set_offset_lfh(st_cdfh,
						  fu_firmware_get_offset(FU_FIRMWARE(zip_file)));

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
	firmware_class->add_magic = fu_zip_firmware_add_magic;
}

static void
fu_zip_firmware_init(FuZipFirmware *self)
{
	fu_firmware_add_image_gtype(FU_FIRMWARE(self), FU_TYPE_ZIP_FILE);
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_STORED_SIZE);
	fu_firmware_set_images_max(FU_FIRMWARE(self), G_MAXUINT16);
}

/**
 * fu_zip_firmware_new:
 *
 * Returns: (transfer full): a #FuZipFirmware
 *
 * Since: 2.1.1
 **/
FuFirmware *
fu_zip_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_ZIP_FIRMWARE, NULL));
}
