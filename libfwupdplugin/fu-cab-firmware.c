/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuCabFirmware"

#include "config.h"

#include <zlib.h>

#include "fu-byte-array.h"
#include "fu-cab-firmware-private.h"
#include "fu-cab-image.h"
#include "fu-cab-struct.h"
#include "fu-chunk-array.h"
#include "fu-common.h"
#include "fu-composite-input-stream.h"
#include "fu-input-stream.h"
#include "fu-mem-private.h"
#include "fu-partial-input-stream.h"
#include "fu-string.h"

typedef struct {
	gboolean compressed;
	gboolean only_basename;
} FuCabFirmwarePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuCabFirmware, fu_cab_firmware, FU_TYPE_FIRMWARE)
#define GET_PRIVATE(o) (fu_cab_firmware_get_instance_private(o))

#define FU_CAB_FIRMWARE_MAX_FILES   1024
#define FU_CAB_FIRMWARE_MAX_FOLDERS 64

#define FU_CAB_FIRMWARE_DECOMPRESS_BUFSZ 0x4000 /* bytes */

/**
 * fu_cab_firmware_get_compressed:
 * @self: a #FuCabFirmware
 *
 * Gets if the cabinet archive should be compressed.
 *
 * Returns: boolean
 *
 * Since: 1.9.7
 **/
gboolean
fu_cab_firmware_get_compressed(FuCabFirmware *self)
{
	FuCabFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_CAB_FIRMWARE(self), FALSE);
	return priv->compressed;
}

/**
 * fu_cab_firmware_set_compressed:
 * @self: a #FuCabFirmware
 * @compressed: boolean
 *
 * Sets if the cabinet archive should be compressed.
 *
 * Since: 1.9.7
 **/
void
fu_cab_firmware_set_compressed(FuCabFirmware *self, gboolean compressed)
{
	FuCabFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_CAB_FIRMWARE(self));
	priv->compressed = compressed;
}

/**
 * fu_cab_firmware_get_only_basename:
 * @self: a #FuCabFirmware
 *
 * Gets if the cabinet archive filenames should have the path component removed.
 *
 * Returns: boolean
 *
 * Since: 1.9.7
 **/
gboolean
fu_cab_firmware_get_only_basename(FuCabFirmware *self)
{
	FuCabFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_CAB_FIRMWARE(self), FALSE);
	return priv->only_basename;
}

/**
 * fu_cab_firmware_set_only_basename:
 * @self: a #FuCabFirmware
 * @only_basename: boolean
 *
 * Sets if the cabinet archive filenames should have the path component removed.
 *
 * Since: 1.9.7
 **/
void
fu_cab_firmware_set_only_basename(FuCabFirmware *self, gboolean only_basename)
{
	FuCabFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_CAB_FIRMWARE(self));
	priv->only_basename = only_basename;
}

typedef struct {
	GInputStream *stream;
	FuFirmwareParseFlags parse_flags;
	gsize rsvd_folder;
	gsize rsvd_block;
	gsize size_total;
	FuCabCompression compression;
	GPtrArray *folder_data; /* of FuCompositeInputStream */
	z_stream zstrm;
	guint8 *decompress_buf;
	gsize decompress_bufsz;
	gsize ndatabsz;
} FuCabFirmwareParseHelper;

static void
fu_cab_firmware_parse_helper_free(FuCabFirmwareParseHelper *helper)
{
	inflateEnd(&helper->zstrm);
	if (helper->stream != NULL)
		g_object_unref(helper->stream);
	if (helper->folder_data != NULL)
		g_ptr_array_unref(helper->folder_data);
	g_free(helper->decompress_buf);
	g_free(helper);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuCabFirmwareParseHelper, fu_cab_firmware_parse_helper_free)

/* compute the MS cabinet checksum */
gboolean
fu_cab_firmware_compute_checksum(const guint8 *buf, gsize bufsz, guint32 *checksum, GError **error)
{
	guint32 tmp = *checksum;
	for (gsize i = 0; i < bufsz; i += 4) {
		gsize chunksz = bufsz - i;
		if (G_LIKELY(chunksz >= 4)) {
			/* 3,2,1,0 */
			tmp ^= ((guint32)buf[i + 3] << 24) | ((guint32)buf[i + 2] << 16) |
			       ((guint32)buf[i + 1] << 8) | (guint32)buf[i + 0];
		} else if (chunksz == 3) {
			/* 0,1,2 -- yes, weird */
			tmp ^= ((guint32)buf[i + 0] << 16) | ((guint32)buf[i + 1] << 8) |
			       (guint32)buf[i + 2];
		} else if (chunksz == 2) {
			/* 0,1 -- yes, weird */
			tmp ^= ((guint32)buf[i + 0] << 8) | (guint32)buf[i + 1];
		} else {
			/* 0 */
			tmp ^= (guint32)buf[i + 0];
		}
	}
	*checksum = tmp;
	return TRUE;
}

static gboolean
fu_cab_firmware_compute_checksum_stream_cb(const guint8 *buf,
					   gsize bufsz,
					   gpointer user_data,
					   GError **error)
{
	guint32 *checksum = (guint32 *)user_data;
	return fu_cab_firmware_compute_checksum(buf, bufsz, checksum, error);
}

static voidpf
fu_cab_firmware_zalloc(voidpf opaque, uInt items, uInt size)
{
	return g_malloc0_n(items, size);
}

static void
fu_cab_firmware_zfree(voidpf opaque, voidpf address)
{
	g_free(address);
}

typedef z_stream z_stream_deflater;

static void
fu_cab_firmware_zstream_deflater_free(z_stream_deflater *zstrm)
{
	deflateEnd(zstrm);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(z_stream_deflater, fu_cab_firmware_zstream_deflater_free)

static gboolean
fu_cab_firmware_parse_data(FuCabFirmware *self,
			   FuCabFirmwareParseHelper *helper,
			   gsize *offset,
			   GInputStream *folder_data,
			   GError **error)
{
	gsize blob_comp;
	gsize blob_uncomp;
	gsize hdr_sz;
	gsize size_max = fu_firmware_get_size_max(FU_FIRMWARE(self));
	g_autoptr(FuStructCabData) st = NULL;
	g_autoptr(GInputStream) partial_stream = NULL;

	/* parse header */
	st = fu_struct_cab_data_parse_stream(helper->stream, *offset, error);
	if (st == NULL)
		return FALSE;

	/* sanity check */
	blob_comp = fu_struct_cab_data_get_comp(st);
	blob_uncomp = fu_struct_cab_data_get_uncomp(st);
	if (helper->compression == FU_CAB_COMPRESSION_NONE && blob_comp != blob_uncomp) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "mismatched compressed data");
		return FALSE;
	}
	helper->size_total += blob_uncomp;
	if (size_max > 0 && helper->size_total > size_max) {
		g_autofree gchar *sz_val = g_format_size(helper->size_total);
		g_autofree gchar *sz_max = g_format_size(size_max);
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "uncompressed data too large (%s, limit %s)",
			    sz_val,
			    sz_max);
		return FALSE;
	}

	hdr_sz = st->len + helper->rsvd_block;

	/* verify checksum */
	partial_stream =
	    fu_partial_input_stream_new(helper->stream, *offset + hdr_sz, blob_comp, error);
	if (partial_stream == NULL) {
		g_prefix_error_literal(error, "failed to cut cabinet checksum: ");
		return FALSE;
	}
	if ((helper->parse_flags & FU_FIRMWARE_PARSE_FLAG_IGNORE_CHECKSUM) == 0) {
		guint32 checksum = fu_struct_cab_data_get_checksum(st);
		if (checksum != 0) {
			guint32 checksum_actual = 0;
			g_autoptr(GByteArray) hdr = g_byte_array_new();

			if (!fu_input_stream_chunkify(partial_stream,
						      fu_cab_firmware_compute_checksum_stream_cb,
						      &checksum_actual,
						      error))
				return FALSE;
			fu_byte_array_append_uint16(hdr, blob_comp, G_LITTLE_ENDIAN);
			fu_byte_array_append_uint16(hdr, blob_uncomp, G_LITTLE_ENDIAN);
			if (!fu_cab_firmware_compute_checksum(hdr->data,
							      hdr->len,
							      &checksum_actual,
							      error))
				return FALSE;
			if (checksum_actual != checksum) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "invalid checksum at 0x%x, expected 0x%x, got 0x%x",
					    (guint)*offset,
					    checksum,
					    checksum_actual);
				return FALSE;
			}
		}
	}

	/* decompress Zlib data after removing *another *header... */
	if (helper->compression == FU_CAB_COMPRESSION_MSZIP) {
		int zret;
		g_autofree gchar *kind = NULL;
		g_autoptr(GByteArray) buf = g_byte_array_new();
		g_autoptr(GBytes) bytes_comp = NULL;
		g_autoptr(GBytes) bytes_uncomp = NULL;

		/* check compressed header */
		bytes_comp = fu_input_stream_read_bytes(helper->stream,
							*offset + hdr_sz,
							blob_comp,
							NULL,
							error);
		if (bytes_comp == NULL)
			return FALSE;
		kind = fu_memstrsafe(g_bytes_get_data(bytes_comp, NULL),
				     g_bytes_get_size(bytes_comp),
				     0x0,
				     2,
				     error);
		if (kind == NULL)
			return FALSE;
		if (g_strcmp0(kind, "CK") != 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "compressed header invalid: %s",
				    kind);
			return FALSE;
		}
		if (helper->decompress_buf == NULL)
			helper->decompress_buf = g_malloc0(helper->decompress_bufsz);
		helper->zstrm.avail_in = g_bytes_get_size(bytes_comp) - 2;
		helper->zstrm.next_in = (z_const Bytef *)g_bytes_get_data(bytes_comp, NULL) + 2;
		while (1) {
			helper->zstrm.avail_out = helper->decompress_bufsz;
			helper->zstrm.next_out = helper->decompress_buf;
			zret = inflate(&helper->zstrm, Z_BLOCK);
			if (zret == Z_STREAM_END)
				break;
			g_byte_array_append(buf,
					    helper->decompress_buf,
					    helper->decompress_bufsz - helper->zstrm.avail_out);
			if (zret != Z_OK) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "inflate error @0x%x: %s",
					    (guint)*offset,
					    zError(zret));
				return FALSE;
			}
		}
		zret = inflateReset(&helper->zstrm);
		if (zret != Z_OK) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "failed to reset inflate: %s",
				    zError(zret));
			return FALSE;
		}
		zret = inflateSetDictionary(&helper->zstrm, buf->data, buf->len);
		if (zret != Z_OK) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "failed to set inflate dictionary: %s",
				    zError(zret));
			return FALSE;
		}
		bytes_uncomp =
		    g_byte_array_free_to_bytes(g_steal_pointer(&buf)); /* nocheck:blocked */
		fu_composite_input_stream_add_bytes(FU_COMPOSITE_INPUT_STREAM(folder_data),
						    bytes_uncomp);
	} else {
		fu_composite_input_stream_add_partial_stream(
		    FU_COMPOSITE_INPUT_STREAM(folder_data),
		    FU_PARTIAL_INPUT_STREAM(partial_stream));
	}

	/* success */
	*offset += blob_comp + hdr_sz;
	return TRUE;
}

static gboolean
fu_cab_firmware_parse_folder(FuCabFirmware *self,
			     FuCabFirmwareParseHelper *helper,
			     guint idx,
			     gsize offset,
			     GInputStream *folder_data,
			     GError **error)
{
	FuCabFirmwarePrivate *priv = GET_PRIVATE(self);
	g_autoptr(GByteArray) st = NULL;

	/* parse header */
	st = fu_struct_cab_folder_parse_stream(helper->stream, offset, error);
	if (st == NULL)
		return FALSE;

	/* sanity check */
	if (fu_struct_cab_folder_get_ndatab(st) == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no CFDATA blocks");
		return FALSE;
	}
	helper->compression = fu_struct_cab_folder_get_compression(st);
	if (helper->compression != FU_CAB_COMPRESSION_NONE)
		priv->compressed = TRUE;
	if (helper->compression != FU_CAB_COMPRESSION_NONE &&
	    helper->compression != FU_CAB_COMPRESSION_MSZIP) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "compression %s not supported",
			    fu_cab_compression_to_string(helper->compression));
		return FALSE;
	}

	/* parse CDATA, either using the stream offset or the per-spec FuStructCabFolder.ndatab */
	if (helper->ndatabsz > 0) {
		for (gsize off = fu_struct_cab_folder_get_offset(st); off < helper->ndatabsz;) {
			if (!fu_cab_firmware_parse_data(self, helper, &off, folder_data, error))
				return FALSE;
		}
	} else {
		gsize off = fu_struct_cab_folder_get_offset(st);
		for (guint16 i = 0; i < fu_struct_cab_folder_get_ndatab(st); i++) {
			if (!fu_cab_firmware_parse_data(self, helper, &off, folder_data, error))
				return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_cab_firmware_parse_file(FuCabFirmware *self,
			   FuCabFirmwareParseHelper *helper,
			   gsize *offset,
			   GError **error)
{
	FuCabFirmwarePrivate *priv = GET_PRIVATE(self);
	GInputStream *folder_data;
	guint16 date;
	guint16 index;
	guint16 time;
	g_autoptr(FuCabImage) img = fu_cab_image_new();
	g_autoptr(GByteArray) st = NULL;
	g_autoptr(GDateTime) created = NULL;
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(GString) filename = g_string_new(NULL);
	g_autoptr(GTimeZone) tz_utc = g_time_zone_new_utc();

	/* parse header */
	st = fu_struct_cab_file_parse_stream(helper->stream, *offset, error);
	if (st == NULL)
		return FALSE;
	fu_firmware_set_offset(FU_FIRMWARE(img), fu_struct_cab_file_get_uoffset(st));
	fu_firmware_set_size(FU_FIRMWARE(img), fu_struct_cab_file_get_usize(st));

	/* sanity check */
	index = fu_struct_cab_file_get_index(st);
	if (index >= helper->folder_data->len) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "failed to get folder data for 0x%x",
			    index);
		return FALSE;
	}
	folder_data = g_ptr_array_index(helper->folder_data, index);

	/* parse filename */
	*offset += FU_STRUCT_CAB_FILE_SIZE;
	for (guint i = 0; i < 255; i++) {
		guint8 value = 0;
		if (!fu_input_stream_read_u8(helper->stream, *offset + i, &value, error))
			return FALSE;
		if (value == 0)
			break;
		if (!g_ascii_isprint((gchar)value)) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "non-ASCII filenames are not supported: 0x%02x",
				    value);
			return FALSE;
		}
		/* convert to UNIX path */
		if (value == '\\')
			value = '/';
		g_string_append_c(filename, (gchar)value);
	}

	/* add image */
	if (priv->only_basename) {
		g_autofree gchar *id = g_path_get_basename(filename->str);
		fu_firmware_set_id(FU_FIRMWARE(img), id);
	} else {
		fu_firmware_set_id(FU_FIRMWARE(img), filename->str);
	}
	stream = fu_partial_input_stream_new(folder_data,
					     fu_struct_cab_file_get_uoffset(st),
					     fu_struct_cab_file_get_usize(st),
					     error);
	if (stream == NULL) {
		g_prefix_error_literal(error, "failed to cut cabinet image: ");
		return FALSE;
	}
	if (!fu_firmware_parse_stream(FU_FIRMWARE(img), stream, 0x0, helper->parse_flags, error))
		return FALSE;
	if (!fu_firmware_add_image_full(FU_FIRMWARE(self), FU_FIRMWARE(img), error))
		return FALSE;

	/* set created date time */
	date = fu_struct_cab_file_get_date(st);
	time = fu_struct_cab_file_get_time(st);
	created = g_date_time_new(tz_utc,
				  1980 + ((date & 0xFE00) >> 9),
				  (date & 0x01E0) >> 5,
				  date & 0x001F,
				  (time & 0xF800) >> 11,
				  (time & 0x07E0) >> 5,
				  (time & 0x001F) * 2);
	fu_cab_image_set_created(img, created);

	/* offset to next entry */
	*offset += filename->len + 1;
	return TRUE;
}

static gboolean
fu_cab_firmware_validate(FuFirmware *firmware, GInputStream *stream, gsize offset, GError **error)
{
	return fu_struct_cab_header_validate_stream(stream, offset, error);
}

static FuCabFirmwareParseHelper *
fu_cab_firmware_parse_helper_new(GInputStream *stream, FuFirmwareParseFlags flags, GError **error)
{
	int zret;
	g_autoptr(FuCabFirmwareParseHelper) helper = g_new0(FuCabFirmwareParseHelper, 1);

	/* zlib */
	helper->zstrm.zalloc = fu_cab_firmware_zalloc;
	helper->zstrm.zfree = fu_cab_firmware_zfree;
	zret = inflateInit2(&helper->zstrm, -MAX_WBITS);
	if (zret != Z_OK) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "failed to initialize inflate: %s",
			    zError(zret));
		return NULL;
	}

	helper->stream = g_object_ref(stream);
	helper->parse_flags = flags;
	helper->folder_data = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	helper->decompress_bufsz = FU_CAB_FIRMWARE_DECOMPRESS_BUFSZ;
	return g_steal_pointer(&helper);
}

static gboolean
fu_cab_firmware_parse(FuFirmware *firmware,
		      GInputStream *stream,
		      FuFirmwareParseFlags flags,
		      GError **error)
{
	FuCabFirmware *self = FU_CAB_FIRMWARE(firmware);
	gsize off_cffile = 0;
	gsize offset = 0;
	gsize streamsz = 0;
	g_autoptr(GByteArray) st = NULL;
	g_autoptr(FuCabFirmwareParseHelper) helper = NULL;

	/* get size */
	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;

	/* parse header */
	st = fu_struct_cab_header_parse_stream(stream, offset, error);
	if (st == NULL)
		return FALSE;

	/* sanity checks */
	if (fu_struct_cab_header_get_size(st) < streamsz) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "buffer size 0x%x is less than stream size 0x%x",
			    (guint)streamsz,
			    fu_struct_cab_header_get_size(st));
		return FALSE;
	}
	if (fu_struct_cab_header_get_idx_cabinet(st) != 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "chained archive not supported");
		return FALSE;
	}
	if (fu_struct_cab_header_get_nr_folders(st) == 0 ||
	    fu_struct_cab_header_get_nr_files(st) == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "archive is empty");
		return FALSE;
	}
	if (fu_struct_cab_header_get_nr_folders(st) > FU_CAB_FIRMWARE_MAX_FOLDERS) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "too many CFFOLDERS, parsed %u and limit was %u",
			    fu_struct_cab_header_get_nr_folders(st),
			    (guint)FU_CAB_FIRMWARE_MAX_FOLDERS);
		return FALSE;
	}
	if (fu_struct_cab_header_get_nr_files(st) > FU_CAB_FIRMWARE_MAX_FILES) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "too many CFFILES, parsed %u and limit was %u",
			    fu_struct_cab_header_get_nr_files(st),
			    (guint)FU_CAB_FIRMWARE_MAX_FILES);
		return FALSE;
	}
	off_cffile = fu_struct_cab_header_get_off_cffile(st);
	if (off_cffile > streamsz) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "archive is corrupt");
		return FALSE;
	}

	/* create helper */
	helper = fu_cab_firmware_parse_helper_new(stream, flags, error);
	if (helper == NULL)
		return FALSE;

	/* if the only folder is >= 2GB then FuStructCabFolder.ndatab will overflow */
	if (streamsz >= 0x8000 * 0xFFFF && fu_struct_cab_header_get_nr_folders(st) == 1)
		helper->ndatabsz = streamsz;

	/* reserved sizes */
	offset += st->len;
	if (fu_struct_cab_header_get_flags(st) & 0x0004) {
		g_autoptr(GByteArray) st2 = NULL;
		st2 = fu_struct_cab_header_reserve_parse_stream(stream, offset, error);
		if (st2 == NULL)
			return FALSE;
		offset += st2->len;
		offset += fu_struct_cab_header_reserve_get_rsvd_hdr(st2);
		helper->rsvd_block = fu_struct_cab_header_reserve_get_rsvd_block(st2);
		helper->rsvd_folder = fu_struct_cab_header_reserve_get_rsvd_folder(st2);
	}

	/* parse CFFOLDER */
	for (guint i = 0; i < fu_struct_cab_header_get_nr_folders(st); i++) {
		g_autoptr(GInputStream) folder_data = fu_composite_input_stream_new();
		if (!fu_cab_firmware_parse_folder(self, helper, i, offset, folder_data, error))
			return FALSE;
		if (!fu_input_stream_size(folder_data, &streamsz, error))
			return FALSE;
		if (streamsz == 0) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "no folder data");
			return FALSE;
		}
		g_ptr_array_add(helper->folder_data, g_steal_pointer(&folder_data));
		offset += FU_STRUCT_CAB_FOLDER_SIZE + helper->rsvd_folder;
	}

	/* parse CFFILEs */
	for (guint i = 0; i < fu_struct_cab_header_get_nr_files(st); i++) {
		if (!fu_cab_firmware_parse_file(self, helper, &off_cffile, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static GByteArray *
fu_cab_firmware_write(FuFirmware *firmware, GError **error)
{
	FuCabFirmware *self = FU_CAB_FIRMWARE(firmware);
	FuCabFirmwarePrivate *priv = GET_PRIVATE(self);
	gsize archive_size;
	gsize offset;
	guint32 index_into = 0;
	g_autoptr(GByteArray) st_hdr = fu_struct_cab_header_new();
	g_autoptr(GByteArray) st_folder = fu_struct_cab_folder_new();
	g_autoptr(GPtrArray) imgs = fu_firmware_get_images(firmware);
	g_autoptr(GByteArray) cfdata_linear = g_byte_array_new();
	g_autoptr(GBytes) cfdata_linear_blob = NULL;
	g_autoptr(FuChunkArray) chunks = NULL;
	g_autoptr(GPtrArray) chunks_zlib =
	    g_ptr_array_new_with_free_func((GDestroyNotify)g_byte_array_unref);

	/* create linear CFDATA block */
	for (guint i = 0; i < imgs->len; i++) {
		FuFirmware *img = g_ptr_array_index(imgs, i);
		const gchar *filename_win32 = fu_cab_image_get_win32_filename(FU_CAB_IMAGE(img));
		g_autoptr(GBytes) img_blob = NULL;

		if (filename_win32 == NULL) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "no image filename");
			return NULL;
		}
		img_blob = fu_firmware_get_bytes(img, error);
		if (img_blob == NULL)
			return NULL;
		fu_byte_array_append_bytes(cfdata_linear, img_blob);
	}

	/* chunkify and compress with a fixed size */
	if (cfdata_linear->len == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no data to compress");
		return NULL;
	}
	cfdata_linear_blob =
	    g_byte_array_free_to_bytes(g_steal_pointer(&cfdata_linear)); /* nocheck:blocked */
	chunks = fu_chunk_array_new_from_bytes(cfdata_linear_blob,
					       FU_CHUNK_ADDR_OFFSET_NONE,
					       FU_CHUNK_PAGESZ_NONE,
					       0x8000);
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;
		g_autoptr(GByteArray) chunk_zlib = g_byte_array_new();
		g_autoptr(GByteArray) buf = g_byte_array_new();

		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return NULL;
		fu_byte_array_set_size(chunk_zlib, fu_chunk_get_data_sz(chk) * 2, 0x0);
		if (priv->compressed) {
			int zret;
			z_stream zstrm = {
			    .zalloc = fu_cab_firmware_zalloc,
			    .zfree = fu_cab_firmware_zfree,
			    .opaque = Z_NULL,
			    .next_in = (guint8 *)fu_chunk_get_data(chk),
			    .avail_in = fu_chunk_get_data_sz(chk),
			    .next_out = chunk_zlib->data,
			    .avail_out = chunk_zlib->len,
			};
			g_autoptr(z_stream_deflater) zstrm_deflater = &zstrm;
			zret = deflateInit2(zstrm_deflater,
					    Z_DEFAULT_COMPRESSION,
					    Z_DEFLATED,
					    -15,
					    8,
					    Z_DEFAULT_STRATEGY);
			if (zret != Z_OK) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "failed to initialize deflate: %s",
					    zError(zret));
				return NULL;
			}
			zret = deflate(zstrm_deflater, Z_FINISH);
			if (zret != Z_OK && zret != Z_STREAM_END) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "zlib deflate failed: %s",
					    zError(zret));
				return NULL;
			}
			fu_byte_array_append_uint8(buf, (guint8)'C');
			fu_byte_array_append_uint8(buf, (guint8)'K');
			g_byte_array_append(buf, chunk_zlib->data, zstrm.total_out);
		} else {
			g_byte_array_append(buf, fu_chunk_get_data(chk), fu_chunk_get_data_sz(chk));
		}
		g_ptr_array_add(chunks_zlib, g_steal_pointer(&buf));
	}

	/* create header */
	archive_size = FU_STRUCT_CAB_HEADER_SIZE;
	archive_size += FU_STRUCT_CAB_FOLDER_SIZE;
	for (guint i = 0; i < imgs->len; i++) {
		FuFirmware *img = g_ptr_array_index(imgs, i);
		const gchar *filename_win32 = fu_cab_image_get_win32_filename(FU_CAB_IMAGE(img));
		archive_size += FU_STRUCT_CAB_FILE_SIZE + strlen(filename_win32) + 1;
	}
	for (guint i = 0; i < chunks_zlib->len; i++) {
		GByteArray *chunk = g_ptr_array_index(chunks_zlib, i);
		archive_size += FU_STRUCT_CAB_DATA_SIZE + chunk->len;
	}
	offset = FU_STRUCT_CAB_HEADER_SIZE;
	offset += FU_STRUCT_CAB_FOLDER_SIZE;
	fu_struct_cab_header_set_size(st_hdr, archive_size);
	fu_struct_cab_header_set_off_cffile(st_hdr, offset);
	fu_struct_cab_header_set_nr_files(st_hdr, imgs->len);

	/* create folder */
	for (guint i = 0; i < imgs->len; i++) {
		FuFirmware *img = g_ptr_array_index(imgs, i);
		const gchar *filename_win32 = fu_cab_image_get_win32_filename(FU_CAB_IMAGE(img));
		offset += FU_STRUCT_CAB_FILE_SIZE;
		offset += strlen(filename_win32) + 1;
	}
	fu_struct_cab_folder_set_offset(st_folder, offset);
	fu_struct_cab_folder_set_ndatab(st_folder, fu_chunk_array_length(chunks));
	fu_struct_cab_folder_set_compression(st_folder,
					     priv->compressed ? FU_CAB_COMPRESSION_MSZIP
							      : FU_CAB_COMPRESSION_NONE);
	g_byte_array_append(st_hdr, st_folder->data, st_folder->len);

	/* create each CFFILE */
	for (guint i = 0; i < imgs->len; i++) {
		FuFirmware *img = g_ptr_array_index(imgs, i);
		FuCabFileAttribute fattr = FU_CAB_FILE_ATTRIBUTE_NONE;
		GDateTime *created = fu_cab_image_get_created(FU_CAB_IMAGE(img));
		const gchar *filename_win32 = fu_cab_image_get_win32_filename(FU_CAB_IMAGE(img));
		g_autoptr(GByteArray) st_file = fu_struct_cab_file_new();
		g_autoptr(GBytes) img_blob = fu_firmware_get_bytes(img, NULL);

		if (!g_str_is_ascii(filename_win32))
			fattr |= FU_CAB_FILE_ATTRIBUTE_NAME_UTF8;
		fu_struct_cab_file_set_fattr(st_file, fattr);
		fu_struct_cab_file_set_usize(st_file, g_bytes_get_size(img_blob));
		fu_struct_cab_file_set_uoffset(st_file, index_into);
		if (created != NULL) {
			fu_struct_cab_file_set_date(st_file,
						    ((g_date_time_get_year(created) - 1980) << 9) +
							(g_date_time_get_month(created) << 5) +
							g_date_time_get_day_of_month(created));
			fu_struct_cab_file_set_time(st_file,
						    (g_date_time_get_hour(created) << 11) +
							(g_date_time_get_minute(created) << 5) +
							(g_date_time_get_second(created) / 2));
		}
		g_byte_array_append(st_hdr, st_file->data, st_file->len);

		g_byte_array_append(st_hdr, (const guint8 *)filename_win32, strlen(filename_win32));
		fu_byte_array_append_uint8(st_hdr, 0x0);
		index_into += g_bytes_get_size(img_blob);
	}

	/* create each CFDATA */
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		guint32 checksum = 0;
		GByteArray *chunk_zlib = g_ptr_array_index(chunks_zlib, i);
		g_autoptr(FuChunk) chk = NULL;
		g_autoptr(GByteArray) hdr = g_byte_array_new();
		g_autoptr(GByteArray) st_data = fu_struct_cab_data_new();

		/* prepare chunk */
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return NULL;

		/* first do the 'checksum' on the data, then the partial header -- slightly crazy */
		if (!fu_cab_firmware_compute_checksum(chunk_zlib->data,
						      chunk_zlib->len,
						      &checksum,
						      error))
			return NULL;
		fu_byte_array_append_uint16(hdr, chunk_zlib->len, G_LITTLE_ENDIAN);
		fu_byte_array_append_uint16(hdr, fu_chunk_get_data_sz(chk), G_LITTLE_ENDIAN);
		if (!fu_cab_firmware_compute_checksum(hdr->data, hdr->len, &checksum, error))
			return NULL;

		fu_struct_cab_data_set_checksum(st_data, checksum);
		fu_struct_cab_data_set_comp(st_data, chunk_zlib->len);
		fu_struct_cab_data_set_uncomp(st_data, fu_chunk_get_data_sz(chk));
		g_byte_array_append(st_hdr, st_data->data, st_data->len);
		g_byte_array_append(st_hdr, chunk_zlib->data, chunk_zlib->len);
	}

	/* success */
	return g_steal_pointer(&st_hdr);
}

static gboolean
fu_cab_firmware_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuCabFirmware *self = FU_CAB_FIRMWARE(firmware);
	FuCabFirmwarePrivate *priv = GET_PRIVATE(self);
	const gchar *tmp;

	/* simple properties */
	tmp = xb_node_query_text(n, "compressed", NULL);
	if (tmp != NULL) {
		if (!fu_strtobool(tmp, &priv->compressed, error))
			return FALSE;
	}
	tmp = xb_node_query_text(n, "only_basename", NULL);
	if (tmp != NULL) {
		if (!fu_strtobool(tmp, &priv->only_basename, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_cab_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuCabFirmware *self = FU_CAB_FIRMWARE(firmware);
	FuCabFirmwarePrivate *priv = GET_PRIVATE(self);
	fu_xmlb_builder_insert_kb(bn, "compressed", priv->compressed);
	fu_xmlb_builder_insert_kb(bn, "only_basename", priv->only_basename);
}

static void
fu_cab_firmware_class_init(FuCabFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->validate = fu_cab_firmware_validate;
	firmware_class->parse = fu_cab_firmware_parse;
	firmware_class->write = fu_cab_firmware_write;
	firmware_class->build = fu_cab_firmware_build;
	firmware_class->export = fu_cab_firmware_export;
}

static void
fu_cab_firmware_init(FuCabFirmware *self)
{
	g_type_ensure(FU_TYPE_CAB_IMAGE);
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_STORED_SIZE);
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_CHECKSUM);
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_DEDUPE_ID);
	fu_firmware_set_images_max(FU_FIRMWARE(self), G_MAXUINT16);
}

/**
 * fu_cab_firmware_new:
 *
 * Returns: (transfer full): a #FuCabFirmware
 *
 * Since: 1.9.7
 **/
FuCabFirmware *
fu_cab_firmware_new(void)
{
	return g_object_new(FU_TYPE_CAB_FIRMWARE, NULL);
}
