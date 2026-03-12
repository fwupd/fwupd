/*
 * Copyright 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuFirmware"

#include "config.h"

#include "fu-byte-array.h"
#include "fu-bytes.h"
#include "fu-common.h"
#include "fu-coswid-firmware.h"
#include "fu-input-stream.h"
#include "fu-lzma-common.h"
#include "fu-partial-input-stream.h"
#include "fu-uswid-firmware.h"
#include "fu-uswid-struct.h"

/**
 * FuUswidFirmware:
 *
 * A uSWID header with multiple optionally-compressed SBOM sections.
 *
 * See also: [class@FuCoswidFirmware]
 */

typedef struct {
	guint8 hdrver;
	FuUswidPayloadCompression compression;
	FuUswidPayloadFormat format;
} FuUswidFirmwarePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuUswidFirmware, fu_uswid_firmware, FU_TYPE_FIRMWARE)
#define GET_PRIVATE(o) (fu_uswid_firmware_get_instance_private(o))

#define FU_USWID_FIRMARE_MINIMUM_HDRVER 1

static void
fu_uswid_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuUswidFirmware *self = FU_USWID_FIRMWARE(firmware);
	FuUswidFirmwarePrivate *priv = GET_PRIVATE(self);
	fu_xmlb_builder_insert_kx(bn, "hdrver", priv->hdrver);
	if (priv->compression != FU_USWID_PAYLOAD_COMPRESSION_NONE) {
		fu_xmlb_builder_insert_kv(
		    bn,
		    "compression",
		    fu_uswid_payload_compression_to_string(priv->compression));
	}
	fu_xmlb_builder_insert_kv(bn, "format", fu_uswid_payload_format_to_string(priv->format));
}

static gboolean
fu_uswid_firmware_validate(FuFirmware *firmware, GInputStream *stream, gsize offset, GError **error)
{
	return fu_struct_uswid_validate_stream(stream, offset, error);
}

static GType
fu_uswid_firmware_format_to_gtype(FuUswidPayloadFormat format, GError **error)
{
	if (format == FU_USWID_PAYLOAD_FORMAT_COSWID)
		return FU_TYPE_COSWID_FIRMWARE;
	if (format == FU_USWID_PAYLOAD_FORMAT_CYCLONEDX)
		return FU_TYPE_FIRMWARE;
	if (format == FU_USWID_PAYLOAD_FORMAT_SPDX)
		return FU_TYPE_FIRMWARE;
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    "format 0x%x is not supported",
		    format);
	return G_TYPE_INVALID;
}

static gboolean
fu_uswid_firmware_parse(FuFirmware *firmware,
			GInputStream *stream,
			FuFirmwareParseFlags flags,
			GError **error)
{
	FuUswidFirmware *self = FU_USWID_FIRMWARE(firmware);
	FuUswidFirmwarePrivate *priv = GET_PRIVATE(self);
	GType img_gtype;
	guint16 hdrsz;
	guint32 payloadsz;
	g_autoptr(FuStructUswid) st = NULL;
	g_autoptr(GBytes) payload = NULL;

	/* unpack */
	st = fu_struct_uswid_parse_stream(stream, 0x0, error);
	if (st == NULL)
		return FALSE;

	/* hdrver */
	priv->hdrver = fu_struct_uswid_get_hdrver(st);
	if (priv->hdrver < FU_USWID_FIRMARE_MINIMUM_HDRVER) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "header version was unsupported");
		return FALSE;
	}

	/* hdrsz+payloadsz */
	hdrsz = fu_struct_uswid_get_hdrsz(st);
	payloadsz = fu_struct_uswid_get_payloadsz(st);
	if (payloadsz == 0x0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "payload size is invalid");
		return FALSE;
	}
	fu_firmware_set_size(firmware, hdrsz + payloadsz);

	/* payload compression */
	if (priv->hdrver >= 0x03) {
		if (fu_struct_uswid_get_flags(st) & FU_USWID_HEADER_FLAG_COMPRESSED) {
			priv->compression = fu_struct_uswid_get_compression(st);
		} else {
			priv->compression = FU_USWID_PAYLOAD_COMPRESSION_NONE;
		}
	} else if (priv->hdrver >= 0x02) {
		priv->compression = fu_struct_uswid_get_flags(st) & FU_USWID_HEADER_FLAG_COMPRESSED
					? FU_USWID_PAYLOAD_COMPRESSION_ZLIB
					: FU_USWID_PAYLOAD_COMPRESSION_NONE;
	} else {
		priv->compression = FU_USWID_PAYLOAD_COMPRESSION_NONE;
	}

	/* payload format */
	if (priv->hdrver >= 0x04) {
		priv->format = fu_struct_uswid_get_format(st);
		img_gtype = fu_uswid_firmware_format_to_gtype(priv->format, error);
		if (img_gtype == G_TYPE_INVALID)
			return FALSE;
	} else {
		priv->format = FU_USWID_PAYLOAD_FORMAT_COSWID;
		img_gtype = FU_TYPE_COSWID_FIRMWARE;
	}

	/* zlib stream */
	if (priv->compression == FU_USWID_PAYLOAD_COMPRESSION_ZLIB) {
		g_autoptr(GConverter) conv = NULL;
		g_autoptr(GInputStream) istream1 = NULL;
		g_autoptr(GInputStream) istream2 = NULL;
		conv = G_CONVERTER(g_zlib_decompressor_new(G_ZLIB_COMPRESSOR_FORMAT_ZLIB));
		istream1 = fu_partial_input_stream_new(stream, hdrsz, payloadsz, error);
		if (istream1 == NULL) {
			g_prefix_error_literal(error, "failed to cut uSWID payload: ");
			return FALSE;
		}
		if (!g_seekable_seek(G_SEEKABLE(istream1), 0, G_SEEK_SET, NULL, error))
			return FALSE;
		istream2 = g_converter_input_stream_new(istream1, conv);
		g_filter_input_stream_set_close_base_stream(G_FILTER_INPUT_STREAM(istream2), FALSE);
		payload = fu_input_stream_read_bytes(istream2, 0, G_MAXSIZE, NULL, error);
		if (payload == NULL)
			return FALSE;
	} else if (priv->compression == FU_USWID_PAYLOAD_COMPRESSION_LZMA) {
		g_autoptr(GBytes) payload_tmp = NULL;
		payload_tmp = fu_input_stream_read_bytes(stream, hdrsz, payloadsz, NULL, error);
		if (payload_tmp == NULL)
			return FALSE;
		payload = fu_lzma_decompress_bytes(payload_tmp, 16 * 1024 * 1024, error);
		if (payload == NULL)
			return FALSE;
	} else if (priv->compression == FU_USWID_PAYLOAD_COMPRESSION_NONE) {
		payload = fu_input_stream_read_bytes(stream, hdrsz, payloadsz, NULL, error);
		if (payload == NULL)
			return FALSE;
	} else {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "compression format 0x%x is not supported",
			    priv->compression);
		return FALSE;
	}

	/* payload */
	payloadsz = g_bytes_get_size(payload);
	for (gsize offset_tmp = 0; offset_tmp < payloadsz;) {
		g_autoptr(FuFirmware) img = g_object_new(img_gtype, NULL);
		g_autoptr(GBytes) img_blob = NULL;

		/* parse SBOM component */
		img_blob = fu_bytes_new_offset(payload, offset_tmp, payloadsz - offset_tmp, error);
		if (img_blob == NULL)
			return FALSE;
		if (!fu_firmware_parse_bytes(img,
					     img_blob,
					     0x0,
					     flags | FU_FIRMWARE_PARSE_FLAG_NO_SEARCH,
					     error))
			return FALSE;
		if (!fu_firmware_add_image(firmware, img, error))
			return FALSE;
		if (fu_firmware_get_size(img) == 0) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "read no bytes from uSWID child");
			return FALSE;
		}
		offset_tmp += fu_firmware_get_size(img);
	}

	/* success */
	return TRUE;
}

static GByteArray *
fu_uswid_firmware_write(FuFirmware *firmware, GError **error)
{
	FuUswidFirmware *self = FU_USWID_FIRMWARE(firmware);
	FuUswidFirmwarePrivate *priv = GET_PRIVATE(self);
	FuUswidHeaderFlags flags = FU_USWID_HEADER_FLAG_NONE;
	g_autoptr(FuStructUswid) st = fu_struct_uswid_new();
	g_autoptr(GByteArray) payload = g_byte_array_new();
	g_autoptr(GBytes) payload_blob = NULL;
	g_autoptr(GPtrArray) images = fu_firmware_get_images(firmware);

	/* sanity check */
	if (priv->hdrver > FU_STRUCT_USWID_DEFAULT_HDRVER) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "no idea how to write header format 0x%02x",
			    priv->hdrver);
		return NULL;
	}

	/* generate early so we know the size */
	for (guint i = 0; i < images->len; i++) {
		FuFirmware *img = g_ptr_array_index(images, i);
		g_autoptr(GBytes) fw = fu_firmware_write(img, error);
		if (fw == NULL)
			return NULL;
		fu_byte_array_append_bytes(payload, fw);
	}

	/* compression flag */
	if (priv->compression != FU_USWID_PAYLOAD_COMPRESSION_NONE)
		flags |= FU_USWID_HEADER_FLAG_COMPRESSED;

	/* compression format */
	if (priv->compression == FU_USWID_PAYLOAD_COMPRESSION_ZLIB) {
		g_autoptr(GConverter) conv = NULL;
		g_autoptr(GInputStream) istream1 = NULL;
		g_autoptr(GInputStream) istream2 = NULL;

		conv = G_CONVERTER(g_zlib_compressor_new(G_ZLIB_COMPRESSOR_FORMAT_ZLIB, -1));
		istream1 = g_memory_input_stream_new_from_data(payload->data, payload->len, NULL);
		istream2 = g_converter_input_stream_new(istream1, conv);
		payload_blob = fu_input_stream_read_bytes(istream2, 0, G_MAXSIZE, NULL, error);
		if (payload_blob == NULL)
			return NULL;
	} else if (priv->compression == FU_USWID_PAYLOAD_COMPRESSION_LZMA) {
		g_autoptr(GBytes) payload_tmp = g_bytes_new(payload->data, payload->len);
		payload_blob = fu_lzma_compress_bytes(payload_tmp, error);
		if (payload_blob == NULL)
			return NULL;
	} else {
		payload_blob = g_bytes_new(payload->data, payload->len);
	}

	/* pack */
	fu_struct_uswid_set_hdrver(st, priv->hdrver);
	fu_struct_uswid_set_payloadsz(st, g_bytes_get_size(payload_blob));
	fu_struct_uswid_set_flags(st, flags);
	fu_struct_uswid_set_compression(st, priv->compression);
	fu_struct_uswid_set_format(st, priv->format);

	/* previous headers were smaller in size */
	if (priv->hdrver == 3) {
		g_byte_array_set_size(st->buf, st->buf->len - 1);
	} else if (priv->hdrver == 2) {
		if (priv->compression != FU_USWID_PAYLOAD_COMPRESSION_NONE &&
		    priv->compression != FU_USWID_PAYLOAD_COMPRESSION_ZLIB) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "hdrver 0x02 only supports zlib compression");
			return NULL;
		}
		g_byte_array_set_size(st->buf, st->buf->len - 2);
	} else if (priv->hdrver == 1) {
		g_byte_array_set_size(st->buf, st->buf->len - 3);
	}
	fu_struct_uswid_set_hdrsz(st, st->buf->len);

	/* success */
	fu_byte_array_append_bytes(st->buf, payload_blob);
	return g_steal_pointer(&st->buf);
}

static gboolean
fu_uswid_firmware_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuUswidFirmware *self = FU_USWID_FIRMWARE(firmware);
	FuUswidFirmwarePrivate *priv = GET_PRIVATE(self);
	const gchar *str;
	guint64 tmp;

	/* simple properties */
	tmp = xb_node_query_text_as_uint(n, "hdrver", NULL);
	if (tmp != G_MAXUINT64 && tmp <= G_MAXUINT8)
		priv->hdrver = tmp;

	/* simple properties */
	str = xb_node_query_text(n, "compression", NULL);
	if (str != NULL) {
		priv->compression = fu_uswid_payload_compression_from_string(str);
		if (priv->compression == FU_USWID_PAYLOAD_COMPRESSION_NONE) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "invalid compression type %s",
				    str);
			return FALSE;
		}
	} else {
		priv->compression = FU_USWID_PAYLOAD_COMPRESSION_NONE;
	}

	/* success */
	return TRUE;
}

static void
fu_uswid_firmware_add_magic(FuFirmware *firmware)
{
	fu_firmware_add_magic(firmware,
			      (const guint8 *)FU_STRUCT_USWID_DEFAULT_MAGIC,
			      sizeof(fwupd_guid_t),
			      0x0);
}

static void
fu_uswid_firmware_init(FuUswidFirmware *self)
{
	FuUswidFirmwarePrivate *priv = GET_PRIVATE(self);
	priv->hdrver = FU_USWID_FIRMARE_MINIMUM_HDRVER;
	priv->compression = FU_USWID_PAYLOAD_COMPRESSION_NONE;
	priv->format = FU_USWID_PAYLOAD_FORMAT_COSWID;
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_STORED_SIZE);
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_ALWAYS_SEARCH);
	fu_firmware_set_images_max(FU_FIRMWARE(self), 2000);
	fu_firmware_add_image_gtype(FU_FIRMWARE(self), FU_TYPE_COSWID_FIRMWARE);
	fu_firmware_add_image_gtype(FU_FIRMWARE(self), FU_TYPE_FIRMWARE);
}

static void
fu_uswid_firmware_class_init(FuUswidFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->validate = fu_uswid_firmware_validate;
	firmware_class->parse = fu_uswid_firmware_parse;
	firmware_class->write = fu_uswid_firmware_write;
	firmware_class->build = fu_uswid_firmware_build;
	firmware_class->export = fu_uswid_firmware_export;
	firmware_class->add_magic = fu_uswid_firmware_add_magic;
}

/**
 * fu_uswid_firmware_new:
 *
 * Creates a new #FuFirmware of sub type uSWID
 *
 * Since: 1.8.0
 **/
FuFirmware *
fu_uswid_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_USWID_FIRMWARE, NULL));
}
