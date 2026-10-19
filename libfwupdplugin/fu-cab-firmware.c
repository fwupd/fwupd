/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuCabFirmware"

#include "config.h"

#include "fwupd-rust-cab.h"

#include "fu-byte-array.h"
#include "fu-cab-firmware-private.h"
#include "fu-cab-image.h"
#include "fu-cab-struct.h"
#include "fu-common.h"
#include "fu-input-stream.h"
#include "fu-path.h"
#include "fu-string.h"

typedef struct {
	gboolean compressed;
} FuCabFirmwarePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuCabFirmware, fu_cab_firmware, FU_TYPE_FIRMWARE)
#define GET_PRIVATE(o) (fu_cab_firmware_get_instance_private(o))

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

/* compute the MS cabinet checksum */
gboolean
fu_cab_firmware_compute_checksum(const guint8 *buf, gsize bufsz, guint32 *checksum, GError **error)
{
	guint32 tmp = *checksum;
	while (bufsz != 0) {
		if (G_UNLIKELY(bufsz == 1)) {
			/* 0 */
			tmp ^= (guint32)buf[0];
			break;
		}
		if (G_UNLIKELY(bufsz == 2)) {
			/* 0,1 -- yes, weird */
			tmp ^= ((guint32)buf[0] << 8) | (guint32)buf[1];
			break;
		}
		if (G_UNLIKELY(bufsz == 3)) {
			/* 0,1,2 -- yes, weird, nocheck:endian */
			tmp ^= ((guint32)buf[0] << 16) | ((guint32)buf[1] << 8) | (guint32)buf[2];
			break;
		}
		/* 3,2,1,0 nocheck:endian */
		tmp ^= ((guint32)buf[3] << 24) | ((guint32)buf[2] << 16) | ((guint32)buf[1] << 8) |
		       (guint32)buf[0];
		buf += 4;
		bufsz -= 4;
	}
	*checksum = tmp;
	return TRUE;
}

static gboolean
fu_cab_firmware_validate(FuFirmware *firmware, FuInputStream *stream, gsize offset, GError **error)
{
	return fu_struct_cab_header_validate_stream(stream, offset, error);
}

static gboolean
fu_cab_firmware_parse(FuFirmware *firmware,
		      FuInputStream *stream,
		      FuFirmwareParseFlags flags,
		      GError **error)
{
	FuCabFirmware *self = FU_CAB_FIRMWARE(firmware);
	FuCabFirmwarePrivate *priv = GET_PRIVATE(self);
	FuRsCabParseFlags rs_flags = FU_RS_CAB_PARSE_FLAG_NONE;
	gsize nfiles;
	g_autoptr(FuRsCabArchive) archive = NULL;
	FuRsStreamImpl *stream_impl = NULL;

	stream_impl = fwupd_input_stream_get_impl(stream);
	if (stream_impl == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "stream does not support Rust FFI");
		return FALSE;
	}

	/* map C parse flags to Rust parser flags */
	if (flags & FU_FIRMWARE_PARSE_FLAG_IGNORE_CHECKSUM)
		rs_flags |= FU_RS_CAB_PARSE_FLAG_IGNORE_CHECKSUM;
	if (flags & FU_FIRMWARE_PARSE_FLAG_ONLY_BASENAME)
		rs_flags |= FU_RS_CAB_PARSE_FLAG_ONLY_BASENAME;

	/* parse the archive; stream_impl is borrowed — the archive clones the Arc internally */
	archive = fu_rs_cab_archive_new(stream_impl, 0, rs_flags, error);
	fu_stream_impl_free(stream_impl);
	stream_impl = NULL;
	if (archive == NULL)
		return FALSE;

	/* propagate compressed flag so the write path can round-trip it */
	if (fu_rs_cab_archive_was_compressed(archive))
		priv->compressed = TRUE;

	/* add an image for each file in the archive */
	nfiles = fu_rs_cab_archive_get_nfiles(archive);
	for (gsize i = 0; i < nfiles; i++) {
		FuRsCabFileEntry *file_entry;
		gsize file_size;
		gsize total_read = 0;
		FuRsCabDateTime datetime;
		g_autofree guint8 *buf = NULL;
		g_autoptr(GString) name = NULL;
		g_autoptr(GBytes) blob = NULL;
		g_autoptr(FuCabImage) img = fu_cab_image_new();

		file_entry = fu_rs_cab_archive_get_file(archive, i);
		if (file_entry == NULL) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_READ,
				    "failed to get file entry %zu from CAB archive",
				    i);
			return FALSE;
		}
		file_size = fu_rs_cab_file_entry_get_size(file_entry);
		name = fu_rs_cab_file_entry_get_name_as_unix_path(file_entry, error);
		if (!name || !fu_path_verify_safe(name->str, error))
			return FALSE;

		fu_firmware_set_id(FU_FIRMWARE(img), name->str);
		fu_firmware_set_size(FU_FIRMWARE(img), file_size);

		/* read decompressed file content from the Rust stream */
		buf = g_malloc(file_size > 0 ? file_size : 1);
		while (total_read < file_size) {
			gssize n = fu_rs_cab_file_entry_read(file_entry,
							     buf + total_read,
							     file_size - total_read);
			if (n < 0) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_READ,
					    "failed to read '%s' from CAB archive",
					    name->str);
				return FALSE;
			}
			if (n == 0) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_READ,
					    "%s: truncated read, got %zu out of %zu bytes",
					    name->str,
					    total_read,
					    file_size);
				return FALSE;
			}
			total_read += (gsize)n;
		}
		blob = g_bytes_new(buf, total_read);
		fu_firmware_set_bytes(FU_FIRMWARE(img), blob);

		/* set creation date/time if present. Assume if the year isn't
		 * set the time isn't valid either, we can't distinguish zero
		 * time from midnight */
		fu_rs_cab_file_entry_get_datetime(file_entry, &datetime);
		if (datetime.year != 0) {
			g_autoptr(GTimeZone) tz_utc = g_time_zone_new_utc();
			g_autoptr(GDateTime) created = g_date_time_new(tz_utc,
								       datetime.year,
								       datetime.month,
								       datetime.day,
								       datetime.hour,
								       datetime.minute,
								       datetime.second);
			if (created != NULL)
				fu_cab_image_set_created(img, created);
		}

		if (!fu_firmware_add_image(firmware, FU_FIRMWARE(img), error))
			return FALSE;
	}

	return TRUE;
}

static GByteArray *
fu_cab_firmware_write(FuFirmware *firmware, GError **error)
{
	g_autoptr(GPtrArray) imgs = fu_firmware_get_images(firmware);
	g_autoptr(FuRsCabBuilder) builder = fu_rs_cab_builder_new();
	g_autoptr(GBytes) bytes = NULL;

	gboolean compress = fu_cab_firmware_get_compressed(FU_CAB_FIRMWARE(firmware));
	for (guint i = 0; i < imgs->len; i++) {
		FuFirmware *img = g_ptr_array_index(imgs, i);
		FuCabImage *cab_img = FU_CAB_IMAGE(img);
		const gchar *filename_win32 = fu_cab_image_get_win32_filename(cab_img);
		GDateTime *created = fu_cab_image_get_created(cab_img);
		FuRsCabDateTime datetime = {0};
		g_autoptr(GBytes) img_blob = NULL;
		gconstpointer img_data;
		gsize img_len;

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

		img_data = g_bytes_get_data(img_blob, &img_len);

		if (created != NULL) {
			datetime.year = g_date_time_get_year(created);
			datetime.month = g_date_time_get_month(created);
			datetime.day = g_date_time_get_day_of_month(created);
			datetime.hour = g_date_time_get_hour(created);
			datetime.minute = g_date_time_get_minute(created);
			datetime.second = g_date_time_get_second(created);
		}

		if (!fu_rs_cab_builder_add_file(builder,
						filename_win32,
						img_data,
						img_len,
						&datetime,
						0, /* attributes */
						compress,
						error))
			return NULL;
	}

	bytes = fu_rs_cab_builder_build(builder, error);
	if (bytes == NULL)
		return NULL;

	/* FIXME: this copies the data, need to get rust to write
	 * into a libc buffer */
	return g_bytes_unref_to_array(g_steal_pointer(&bytes));
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

	/* success */
	return TRUE;
}

static void
fu_cab_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuCabFirmware *self = FU_CAB_FIRMWARE(firmware);
	FuCabFirmwarePrivate *priv = GET_PRIVATE(self);
	fu_xmlb_builder_insert_kb(bn, "compressed", priv->compressed);
}

static void
fu_cab_firmware_class_init(FuCabFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	fu_firmware_add_image_gtype(firmware_class, FU_TYPE_CAB_IMAGE);
	firmware_class->validate = fu_cab_firmware_validate;
	firmware_class->parse = fu_cab_firmware_parse;
	firmware_class->write = fu_cab_firmware_write;
	firmware_class->build = fu_cab_firmware_build;
	firmware_class->export = fu_cab_firmware_export;
	fu_firmware_set_images_max(firmware_class, G_MAXUINT16);
#ifdef __x86_64__
	fu_firmware_set_size_max(firmware_class, 16 * FU_GB);
#else
	fu_firmware_set_size_max(firmware_class, 1 * FU_GB);
#endif
}

static void
fu_cab_firmware_init(FuCabFirmware *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_STORED_SIZE);
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_CHECKSUM);
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_DEDUPE_ID);
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
