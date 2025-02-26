/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuCabFirmware"

#include "config.h"

#include <zlib.h>

#include "fu-byte-array.h"
#include "fu-bytes.h"
#include "fu-cab-firmware.h"
#include "fu-cab-image.h"
#include "fu-cab-struct.h"
#include "fu-chunk-array.h"
#include "fu-mem-private.h"
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
	GBytes *fw;
	FwupdInstallFlags install_flags;
	gsize rsvd_folder;
	gsize rsvd_block;
	gsize size_total;
	FuCabCompression compression;
	GPtrArray *folder_data; /* of GBytes */
	z_stream zstrm;
	guint8 *decompress_buf;
	gsize decompress_bufsz;
	gsize ndatabsz;
} FuCabFirmwareParseHelper;

static void
fu_cab_firmware_parse_helper_free(FuCabFirmwareParseHelper *helper)
{
	inflateEnd(&helper->zstrm);
	if (helper->fw != NULL)
		g_bytes_unref(helper->fw);
	if (helper->folder_data != NULL)
		g_ptr_array_unref(helper->folder_data);
	g_free(helper->decompress_buf);
	g_free(helper);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuCabFirmwareParseHelper, fu_cab_firmware_parse_helper_free)

/* compute the MS cabinet checksum */
static guint32
fu_cab_firmware_compute_checksum(const guint8 *buf, gsize bufsz, guint32 seed)
{
	guint32 csum = seed;
	for (gsize i = 0; i < bufsz; i += 4) {
		guint32 ul = 0;
		guint chunksz = MIN(bufsz - i, 4);
		if (chunksz == 4) {
			ul = fu_memread_uint32(buf + i, G_LITTLE_ENDIAN);
		} else if (chunksz == 3) {
			ul = fu_memread_uint24(buf + i, G_BIG_ENDIAN); /* err.. */
		} else if (chunksz == 2) {
			ul = fu_memread_uint16(buf + i, G_BIG_ENDIAN); /* err.. */
		} else if (chunksz == 1) {
			ul = buf[i];
		}
		csum ^= ul;
	}
	return csum;
}

static voidpf
zalloc(voidpf opaque, uInt items, uInt size)
{
	return g_malloc0_n(items, size);
}

static void
zfree(voidpf opaque, voidpf address)
{
	g_free(address);
}

typedef z_stream z_stream_deflater;

static void
zstream_deflater_free(z_stream_deflater *zstrm)
{
	deflateEnd(zstrm);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(z_stream_deflater, zstream_deflater_free)

static gboolean
fu_cab_firmware_parse_data(FuCabFirmware *self,
			   FuCabFirmwareParseHelper *helper,
			   gsize *offset,
			   GByteArray *folder_data,
			   GError **error)
{
	gsize blob_comp;
	gsize blob_uncomp;
	gsize hdr_sz;
	gsize size_max = fu_firmware_get_size_max(FU_FIRMWARE(self));
	g_autoptr(GByteArray) st = NULL;
	g_autoptr(GBytes) data_blob = NULL;

	/* parse header */
	st = fu_struct_cab_data_parse_bytes(helper->fw, *offset, error);
	if (st == NULL)
		return FALSE;

	/* sanity check */
	blob_comp = fu_struct_cab_data_get_comp(st);
	blob_uncomp = fu_struct_cab_data_get_uncomp(st);
	if (helper->compression == FU_CAB_COMPRESSION_NONE && blob_comp != blob_uncomp) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_SUPPORTED,
				    "mismatched compressed data");
		return FALSE;
	}
	helper->size_total += blob_uncomp;
	if (size_max > 0 && helper->size_total > size_max) {
		g_autofree gchar *sz_val = g_format_size(helper->size_total);
		g_autofree gchar *sz_max = g_format_size(size_max);
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "uncompressed data too large (%s, limit %s)",
			    sz_val,
			    sz_max);
		return FALSE;
	}

	hdr_sz = st->len + helper->rsvd_block;
	data_blob = fu_bytes_new_offset(helper->fw, *offset + hdr_sz, blob_comp, error);
	if (data_blob == NULL)
		return FALSE;

	/* verify checksum */
	if ((helper->install_flags & FWUPD_INSTALL_FLAG_IGNORE_CHECKSUM) == 0) {
		guint32 checksum = fu_struct_cab_data_get_checksum(st);
		if (checksum != 0) {
			guint32 checksum_actual =
			    fu_cab_firmware_compute_checksum(g_bytes_get_data(data_blob, NULL),
							     g_bytes_get_size(data_blob),
							     0);
			g_autoptr(GByteArray) hdr = g_byte_array_new();
			fu_byte_array_append_uint16(hdr, blob_comp, G_LITTLE_ENDIAN);
			fu_byte_array_append_uint16(hdr, blob_uncomp, G_LITTLE_ENDIAN);
			checksum_actual =
			    fu_cab_firmware_compute_checksum(hdr->data, hdr->len, checksum_actual);
			if (checksum_actual != checksum) {
				g_set_error(error,
					    G_IO_ERROR,
					    G_IO_ERROR_NOT_SUPPORTED,
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

		/* check compressed header */
		kind = fu_memstrsafe(g_bytes_get_data(data_blob, NULL),
				     g_bytes_get_size(data_blob),
				     0x0,
				     2,
				     error);
		if (kind == NULL)
			return FALSE;
		if (g_strcmp0(kind, "CK") != 0) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_SUPPORTED,
				    "compressed header invalid: %s",
				    kind);
			return FALSE;
		}
		helper->zstrm.avail_in = g_bytes_get_size(data_blob) - 2;
		helper->zstrm.next_in = (z_const Bytef *)g_bytes_get_data(data_blob, NULL) + 2;
		if (helper->decompress_buf == NULL)
			helper->decompress_buf = g_malloc0(helper->decompress_bufsz);
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
					    G_IO_ERROR,
					    G_IO_ERROR_NOT_SUPPORTED,
					    "inflate error @0x%x: %s",
					    (guint)*offset,
					    zError(zret));
				return FALSE;
			}
		}
		zret = inflateReset(&helper->zstrm);
		if (zret != Z_OK) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_SUPPORTED,
				    "failed to reset inflate: %s",
				    zError(zret));
			return FALSE;
		}
		zret = inflateSetDictionary(&helper->zstrm, buf->data, buf->len);
		if (zret != Z_OK) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_SUPPORTED,
				    "failed to set inflate dictionary: %s",
				    zError(zret));
			return FALSE;
		}
		g_byte_array_append(folder_data, buf->data, buf->len);
	} else {
		fu_byte_array_append_bytes(folder_data, data_blob);
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
			     GByteArray *folder_data,
			     GError **error)
{
	FuCabFirmwarePrivate *priv = GET_PRIVATE(self);
	g_autoptr(GByteArray) st = NULL;

	/* parse header */
	st = fu_struct_cab_folder_parse_bytes(helper->fw, offset, error);
	if (st == NULL)
		return FALSE;

	/* sanity check */
	if (fu_struct_cab_folder_get_ndatab(st) == 0) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_SUPPORTED,
				    "no CFDATA blocks");
		return FALSE;
	}
	helper->compression = fu_struct_cab_folder_get_compression(st);
	if (helper->compression != FU_CAB_COMPRESSION_NONE)
		priv->compressed = TRUE;
	if (helper->compression != FU_CAB_COMPRESSION_NONE &&
	    helper->compression != FU_CAB_COMPRESSION_MSZIP) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
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
	GBytes *folder_data;
	gsize bufsz = 0;
	guint16 date;
	guint16 index;
	guint16 time;
	const guint8 *buf = g_bytes_get_data(helper->fw, &bufsz);
	g_autoptr(FuCabImage) img = fu_cab_image_new();
	g_autoptr(GByteArray) st = NULL;
	g_autoptr(GBytes) img_blob = NULL;
	g_autoptr(GDateTime) created = NULL;
	g_autoptr(GString) filename = g_string_new(NULL);
	g_autoptr(GTimeZone) tz_utc = g_time_zone_new_utc();

	/* parse header */
	st = fu_struct_cab_file_parse_bytes(helper->fw, *offset, error);
	if (st == NULL)
		return FALSE;

	/* sanity check */
	index = fu_struct_cab_file_get_index(st);
	if (index >= helper->folder_data->len) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "failed to get folder data for 0x%x",
			    index);
		return FALSE;
	}
	folder_data = g_ptr_array_index(helper->folder_data, index);

	/* parse filename */
	*offset += FU_STRUCT_CAB_FILE_SIZE;
	for (guint i = 0; i < 255; i++) {
		guint8 value = 0;
		if (!fu_memread_uint8_safe(buf, bufsz, *offset + i, &value, error))
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
	fu_firmware_add_image(FU_FIRMWARE(self), FU_FIRMWARE(img));
	img_blob = fu_bytes_new_offset(folder_data,
				       fu_struct_cab_file_get_uoffset(st),
				       fu_struct_cab_file_get_usize(st),
				       error);
	if (img_blob == NULL)
		return FALSE;
	fu_firmware_set_bytes(FU_FIRMWARE(img), img_blob);

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
fu_cab_firmware_check_magic(FuFirmware *firmware, GBytes *fw, gsize offset, GError **error)
{
	return fu_struct_cab_header_validate_bytes(fw, offset, error);
}

static FuCabFirmwareParseHelper *
fu_cab_firmware_parse_helper_new(GBytes *fw, FwupdInstallFlags flags, GError **error)
{
	int zret;
	g_autoptr(FuCabFirmwareParseHelper) helper = g_new0(FuCabFirmwareParseHelper, 1);

	/* zlib */
	helper->zstrm.zalloc = zalloc;
	helper->zstrm.zfree = zfree;
	zret = inflateInit2(&helper->zstrm, -MAX_WBITS);
	if (zret != Z_OK) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "failed to initialize inflate: %s",
			    zError(zret));
		return NULL;
	}

	helper->fw = g_bytes_ref(fw);
	helper->install_flags = flags;
	helper->folder_data = g_ptr_array_new_with_free_func((GDestroyNotify)g_bytes_unref);
	helper->decompress_bufsz = FU_CAB_FIRMWARE_DECOMPRESS_BUFSZ;
	return g_steal_pointer(&helper);
}

static gboolean
fu_cab_firmware_parse(FuFirmware *firmware,
		      GBytes *fw,
		      gsize offset,
		      FwupdInstallFlags flags,
		      GError **error)
{
	FuCabFirmware *self = FU_CAB_FIRMWARE(firmware);
	gsize off_cffile = 0;
	g_autoptr(GByteArray) st = NULL;
	g_autoptr(FuCabFirmwareParseHelper) helper = NULL;

	/* parse header */
	st = fu_struct_cab_header_parse_bytes(fw, offset, error);
	if (st == NULL)
		return FALSE;

	/* sanity checks */
	if (fu_struct_cab_header_get_size(st) < g_bytes_get_size(fw)) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "buffer size 0x%x is less than archive size 0x%x",
			    (guint)g_bytes_get_size(fw),
			    fu_struct_cab_header_get_size(st));
		return FALSE;
	}
	if (fu_struct_cab_header_get_idx_cabinet(st) != 0) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_SUPPORTED,
				    "chained archive not supported");
		return FALSE;
	}
	if (fu_struct_cab_header_get_nr_folders(st) == 0 ||
	    fu_struct_cab_header_get_nr_files(st) == 0) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_SUPPORTED,
				    "archive is empty");
		return FALSE;
	}
	if (fu_struct_cab_header_get_nr_folders(st) > FU_CAB_FIRMWARE_MAX_FOLDERS) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "too many CFFOLDERS, parsed %u and limit was %u",
			    fu_struct_cab_header_get_nr_folders(st),
			    (guint)FU_CAB_FIRMWARE_MAX_FOLDERS);
		return FALSE;
	}
	if (fu_struct_cab_header_get_nr_files(st) > FU_CAB_FIRMWARE_MAX_FILES) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "too many CFFILES, parsed %u and limit was %u",
			    fu_struct_cab_header_get_nr_files(st),
			    (guint)FU_CAB_FIRMWARE_MAX_FILES);
		return FALSE;
	}
	off_cffile = fu_struct_cab_header_get_off_cffile(st);
	if (off_cffile > g_bytes_get_size(fw)) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_SUPPORTED,
				    "archive is corrupt");
		return FALSE;
	}

	/* create helper */
	helper = fu_cab_firmware_parse_helper_new(fw, flags, error);
	if (helper == NULL)
		return FALSE;

	/* if the only folder is >= 2GB then FuStructCabFolder.ndatab will overflow */
	if (g_bytes_get_size(fw) >= 0x8000 * 0xFFFF && fu_struct_cab_header_get_nr_folders(st) == 1)
		helper->ndatabsz = g_bytes_get_size(fw);

	/* reserved sizes */
	offset += st->len;
	if (fu_struct_cab_header_get_flags(st) & 0x0004) {
		g_autoptr(GByteArray) st2 = NULL;
		st2 = fu_struct_cab_header_parse_bytes(fw, offset, error);
		if (st2 == NULL)
			return FALSE;
		offset += st2->len;
		offset += fu_struct_cab_header_reserve_get_rsvd_hdr(st2);
		helper->rsvd_block = fu_struct_cab_header_reserve_get_rsvd_block(st2);
		helper->rsvd_folder = fu_struct_cab_header_reserve_get_rsvd_folder(st2);
	}

	/* parse CFFOLDER */
	for (guint i = 0; i < fu_struct_cab_header_get_nr_folders(st); i++) {
		g_autoptr(GByteArray) folder_data = g_byte_array_new();
		if (!fu_cab_firmware_parse_folder(self, helper, i, offset, folder_data, error))
			return FALSE;
		if (folder_data->len == 0) {
			g_set_error_literal(error,
					    G_IO_ERROR,
					    G_IO_ERROR_NOT_SUPPORTED,
					    "no folder data");
			return FALSE;
		}
		g_ptr_array_add(
		    helper->folder_data,
		    g_byte_array_free_to_bytes(g_steal_pointer(&folder_data))); /* nocheck */
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
					    G_IO_ERROR,
					    G_IO_ERROR_NOT_SUPPORTED,
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
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_SUPPORTED,
				    "no data to compress");
		return NULL;
	}
	cfdata_linear_blob =
	    g_byte_array_free_to_bytes(g_steal_pointer(&cfdata_linear)); /* nocheck */
	chunks = fu_chunk_array_new_from_bytes(cfdata_linear_blob, 0x0, 0x8000);
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = fu_chunk_array_index(chunks, i);
		g_autoptr(GByteArray) chunk_zlib = g_byte_array_new();
		g_autoptr(GByteArray) buf = g_byte_array_new();
		fu_byte_array_set_size(chunk_zlib, fu_chunk_get_data_sz(chk) * 2, 0x0);
		if (priv->compressed) {
			int zret;
			z_stream zstrm = {
			    .zalloc = zalloc,
			    .zfree = zfree,
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
					    G_IO_ERROR,
					    G_IO_ERROR_NOT_SUPPORTED,
					    "failed to initialize deflate: %s",
					    zError(zret));
				return NULL;
			}
			zret = deflate(zstrm_deflater, Z_FINISH);
			if (zret != Z_OK && zret != Z_STREAM_END) {
				g_set_error(error,
					    G_IO_ERROR,
					    G_IO_ERROR_NOT_SUPPORTED,
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
		guint32 checksum;
		GByteArray *chunk_zlib = g_ptr_array_index(chunks_zlib, i);
		g_autoptr(FuChunk) chk = fu_chunk_array_index(chunks, i);
		g_autoptr(GByteArray) hdr = g_byte_array_new();
		g_autoptr(GByteArray) st_data = fu_struct_cab_data_new();

		/* first do the 'checksum' on the data, then the partial header -- slightly crazy */
		checksum = fu_cab_firmware_compute_checksum(chunk_zlib->data, chunk_zlib->len, 0);
		fu_byte_array_append_uint16(hdr, chunk_zlib->len, G_LITTLE_ENDIAN);
		fu_byte_array_append_uint16(hdr, fu_chunk_get_data_sz(chk), G_LITTLE_ENDIAN);
		checksum = fu_cab_firmware_compute_checksum(hdr->data, hdr->len, checksum);

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
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->check_magic = fu_cab_firmware_check_magic;
	klass_firmware->parse = fu_cab_firmware_parse;
	klass_firmware->write = fu_cab_firmware_write;
	klass_firmware->build = fu_cab_firmware_build;
	klass_firmware->export = fu_cab_firmware_export;
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
