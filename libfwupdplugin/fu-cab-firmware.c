/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuCabFirmware"

#include "config.h"

#include "fu-cab-firmware.h"
#include "fu-cab-image.h"
#include "fu-cab-struct.h"
#include "fu-common.h"
#include "fu-composite-input-stream.h"
#include "fu-partial-input-stream.h"
#include "fu-rs-cab.h"
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

/* obsolete: checksum is now computed in the Rust CAB parser */
#if 0
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
#endif

static gboolean
fu_cab_firmware_validate(FuFirmware *firmware, GInputStream *stream, gsize offset, GError **error)
{
	/* use the auto-generated struct validator rather than fu_rs_cab_validate()
	 * as the framework calls this at every offset during brute-force magic
	 * search and the Rust version is too expensive per-call */
	return fu_struct_cab_header_validate_stream(stream, offset, error);
}

static gboolean
fu_cab_firmware_parse(FuFirmware *firmware,
		      GInputStream *stream,
		      FuFirmwareParseFlags flags,
		      GError **error)
{
	FuCabFirmware *self = FU_CAB_FIRMWARE(firmware);
	FuCabFirmwarePrivate *priv = GET_PRIVATE(self);
	int only_basename = (flags & FU_FIRMWARE_PARSE_FLAG_ONLY_BASENAME) ? 1 : 0;
	int ignore_checksum = (flags & FU_FIRMWARE_PARSE_FLAG_IGNORE_CHECKSUM) ? 1 : 0;
	g_autoptr(FuRsCabArchive) archive = NULL; /* nocheck:name */

	/* delegate to rust FFI */
	archive = fu_rs_cab_parse(stream, only_basename, ignore_checksum, error);
	if (archive == NULL)
		return FALSE;

	/* record whether the archive was compressed */
	priv->compressed = fu_rs_cab_is_compressed(archive) != 0;

	/* iterate results creating FuCabImage children */
	for (gsize i = 0; i < fu_rs_cab_nfiles(archive); i++) {
		const gchar *filename = fu_rs_cab_filename(archive, i);
		g_autoptr(FuCabImage) img = fu_cab_image_new();
		g_autoptr(GDateTime) created = NULL;
		g_autoptr(GTimeZone) tz_utc = g_time_zone_new_utc();

		/* set filename as image id */
		fu_firmware_set_id(FU_FIRMWARE(img), filename);

		/* set file data -- zero-copy for uncompressed, bytes for compressed */
		if (fu_rs_cab_file_is_deferred(archive, i)) {
			/* uncompressed: create stream views into the original source */
			gsize nspans = fu_rs_cab_file_nspans(archive, i);
			g_autoptr(GInputStream) file_stream = NULL;
			if (nspans == 1) {
				gsize offset = 0;
				gsize length = 0;
				fu_rs_cab_file_span(archive, i, 0, &offset, &length);
				file_stream = fu_partial_input_stream_new(
				    stream, offset, length, error);
				if (file_stream == NULL)
					return FALSE;
			} else {
				g_autoptr(GInputStream) composite =
				    fu_composite_input_stream_new();
				for (gsize j = 0; j < nspans; j++) {
					gsize offset = 0;
					gsize length = 0;
					g_autoptr(GInputStream) partial = NULL;
					fu_rs_cab_file_span(archive, i, j,
							    &offset, &length);
					partial = fu_partial_input_stream_new(
					    stream, offset, length, error);
					if (partial == NULL)
						return FALSE;
					if (!fu_composite_input_stream_add_stream(
						FU_COMPOSITE_INPUT_STREAM(composite),
						partial, error))
						return FALSE;
				}
				file_stream = g_steal_pointer(&composite);
			}
			if (!fu_firmware_set_stream(FU_FIRMWARE(img),
						    file_stream, error))
				return FALSE;
		} else {
			/* compressed: data was decompressed by the Rust parser */
			gsize data_len = 0;
			const guint8 *data =
			    fu_rs_cab_file_data(archive, i, &data_len);
			g_autoptr(GBytes) blob = g_bytes_new(data, data_len);
			fu_firmware_set_bytes(FU_FIRMWARE(img), blob);
		}

		/* set created date/time from unpacked MS-DOS values */
		created = g_date_time_new(tz_utc,
					  fu_rs_cab_file_year(archive, i),
					  fu_rs_cab_file_month(archive, i),
					  fu_rs_cab_file_day(archive, i),
					  fu_rs_cab_file_hour(archive, i),
					  fu_rs_cab_file_minute(archive, i),
					  fu_rs_cab_file_second(archive, i));
		if (created != NULL)
			fu_cab_image_set_created(img, created);

		/* add image */
		if (!fu_firmware_add_image(FU_FIRMWARE(self), FU_FIRMWARE(img), error))
			return FALSE;
	}

	return TRUE;
}

static GByteArray *
fu_cab_firmware_write(FuFirmware *firmware, GError **error)
{
	FuCabFirmware *self = FU_CAB_FIRMWARE(firmware);
	FuCabFirmwarePrivate *priv = GET_PRIVATE(self);
	gsize out_len = 0;
	guint8 *out_buf = NULL;
	g_autoptr(GPtrArray) imgs = fu_firmware_get_images(firmware);
	g_autoptr(GByteArray) result = NULL;
	g_autoptr(FuRsCabArchive) archive = fu_rs_cab_new(); /* nocheck:name */

	/* build archive from images */
	for (guint i = 0; i < imgs->len; i++) {
		FuFirmware *img = g_ptr_array_index(imgs, i);
		const gchar *filename_win32 = fu_cab_image_get_win32_filename(FU_CAB_IMAGE(img));
		GDateTime *created = fu_cab_image_get_created(FU_CAB_IMAGE(img));
		guint16 year = 0;
		guint8 month = 0;
		guint8 day = 0;
		guint8 hour = 0;
		guint8 minute = 0;
		guint second = 0;
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

		/* extract date/time from GDateTime if available */
		if (created != NULL) {
			year = g_date_time_get_year(created);
			month = g_date_time_get_month(created);
			day = g_date_time_get_day_of_month(created);
			hour = g_date_time_get_hour(created);
			minute = g_date_time_get_minute(created);
			second = g_date_time_get_second(created);
		}

		fu_rs_cab_add_file(archive,
				   filename_win32,
				   g_bytes_get_data(img_blob, NULL),
				   g_bytes_get_size(img_blob),
				   year,
				   month,
				   day,
				   hour,
				   minute,
				   second,
				   0x00);
	}

	/* delegate write to rust FFI */
	out_buf = fu_rs_cab_write(archive, priv->compressed ? 1 : 0, &out_len, error);
	if (out_buf == NULL)
		return NULL;

	/* wrap in a GByteArray (takes ownership of the g_memdup2'd buffer) */
	result = g_byte_array_new_take(out_buf, out_len);
	return g_steal_pointer(&result);
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
	firmware_class->validate = fu_cab_firmware_validate;
	firmware_class->parse = fu_cab_firmware_parse;
	firmware_class->write = fu_cab_firmware_write;
	firmware_class->build = fu_cab_firmware_build;
	firmware_class->export = fu_cab_firmware_export;
}

static void
fu_cab_firmware_init(FuCabFirmware *self)
{
	fu_firmware_add_image_gtype(FU_FIRMWARE(self), FU_TYPE_CAB_IMAGE);
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_STORED_SIZE);
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_CHECKSUM);
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_DEDUPE_ID);
	fu_firmware_set_images_max(FU_FIRMWARE(self), G_MAXUINT16);
#ifdef __x86_64__
	fu_firmware_set_size_max(FU_FIRMWARE(self), 16 * FU_GB);
#else
	fu_firmware_set_size_max(FU_FIRMWARE(self), 1 * FU_GB);
#endif
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
