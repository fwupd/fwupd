/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuFirmware"

#include "config.h"

#include "fu-byte-array.h"
#include "fu-bytes.h"
#include "fu-common.h"
#include "fu-coswid-firmware.h"
#include "fu-mem.h"
#include "fu-string.h"
#include "fu-uswid-firmware.h"

/**
 * FuUswidFirmware:
 *
 * A uSWID header with multiple optionally-compressed coSWID CBOR sections.
 *
 * See also: [class@FuCoswidFirmware]
 */

typedef struct {
	guint8 hdrver;
	gboolean compressed;
} FuUswidFirmwarePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuUswidFirmware, fu_uswid_firmware, FU_TYPE_FIRMWARE)
#define GET_PRIVATE(o) (fu_uswid_firmware_get_instance_private(o))

#define USWID_HEADER_VERSION_V1 1
#define USWID_HEADER_SIZE_V1	23
#define USWID_HEADER_SIZE_V2	24

#define USWID_HEADER_FLAG_COMPRESSED 0x01

typedef struct __attribute__((packed)) {
	guint8 magic[16];
	guint8 hdrver;
	guint16 hdrsz;
	guint32 payloadsz;
	guint8 flags;
} FuUswidFirmwareHdr;

const guint8 USWID_HEADER_MAGIC[] = {0x53,
				     0x42,
				     0x4F,
				     0x4D,
				     0xD6,
				     0xBA,
				     0x2E,
				     0xAC,
				     0xA3,
				     0xE6,
				     0x7A,
				     0x52,
				     0xAA,
				     0xEE,
				     0x3B,
				     0xAF};

static void
fu_uswid_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuUswidFirmware *self = FU_USWID_FIRMWARE(firmware);
	FuUswidFirmwarePrivate *priv = GET_PRIVATE(self);
	fu_xmlb_builder_insert_kx(bn, "hdrver", priv->hdrver);
	fu_xmlb_builder_insert_kb(bn, "compressed", priv->compressed);
}

static gboolean
fu_uswid_firmware_check_magic(FuFirmware *firmware, GBytes *fw, gsize offset, GError **error)
{
	guint8 magic[16] = {0x0};

	if (!fu_memcpy_safe(magic,
			    sizeof(magic),
			    0, /* dst */
			    g_bytes_get_data(fw, NULL),
			    g_bytes_get_size(fw),
			    offset,
			    sizeof(magic),
			    error)) {
		g_prefix_error(error, "failed to read magic: ");
		return FALSE;
	}
	if (memcmp(magic, USWID_HEADER_MAGIC, sizeof(magic)) != 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "invalid magic for file");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_uswid_firmware_parse(FuFirmware *firmware,
			GBytes *fw,
			gsize offset,
			FwupdInstallFlags flags,
			GError **error)
{
	FuUswidFirmware *self = FU_USWID_FIRMWARE(firmware);
	FuUswidFirmwarePrivate *priv = GET_PRIVATE(self);
	guint16 hdrsz = 0;
	guint32 payloadsz = 0;
	guint8 uswid_flags = 0;
	gsize bufsz;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);
	g_autoptr(GBytes) payload = NULL;

	/* hdrver */
	if (!fu_memread_uint8_safe(buf,
				   bufsz,
				   offset + G_STRUCT_OFFSET(FuUswidFirmwareHdr, hdrver),
				   &priv->hdrver,
				   error))
		return FALSE;
	if (priv->hdrver < USWID_HEADER_VERSION_V1) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "header version was unsupported");
		return FALSE;
	}

	/* hdrsz */
	if (!fu_memread_uint16_safe(buf,
				    bufsz,
				    offset + G_STRUCT_OFFSET(FuUswidFirmwareHdr, hdrsz),
				    &hdrsz,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	if (hdrsz < USWID_HEADER_SIZE_V1) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "header size is invalid");
		return FALSE;
	}

	/* payloadsz */
	if (!fu_memread_uint32_safe(buf,
				    bufsz,
				    offset + G_STRUCT_OFFSET(FuUswidFirmwareHdr, payloadsz),
				    &payloadsz,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	if (payloadsz == 0x0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "payload size is invalid");
		return FALSE;
	}
	fu_firmware_set_size(firmware, hdrsz + payloadsz);

	/* flags */
	if (priv->hdrver >= 0x02) {
		if (!fu_memread_uint8_safe(buf,
					   bufsz,
					   offset + G_STRUCT_OFFSET(FuUswidFirmwareHdr, flags),
					   &uswid_flags,
					   error))
			return FALSE;
		priv->compressed = (uswid_flags & USWID_HEADER_FLAG_COMPRESSED) > 0;
	}

	/* zlib stream */
	if (priv->compressed) {
		g_autoptr(GBytes) payload_tmp = NULL;
		g_autoptr(GConverter) conv = NULL;
		g_autoptr(GInputStream) istream1 = NULL;
		g_autoptr(GInputStream) istream2 = NULL;

		payload_tmp = fu_bytes_new_offset(fw, offset + hdrsz, payloadsz, error);
		if (payload_tmp == NULL)
			return FALSE;
		istream1 = g_memory_input_stream_new_from_bytes(payload_tmp);
		conv = G_CONVERTER(g_zlib_decompressor_new(G_ZLIB_COMPRESSOR_FORMAT_ZLIB));
		istream2 = g_converter_input_stream_new(istream1, conv);
		payload = fu_bytes_get_contents_stream(istream2, G_MAXSIZE, error);
		if (payload == NULL)
			return FALSE;
		payloadsz = g_bytes_get_size(payload);
	} else {
		payload = fu_bytes_new_offset(fw, offset + hdrsz, payloadsz, error);
		if (payload == NULL)
			return FALSE;
	}

	/* payload */
	for (gsize offset_tmp = 0; offset_tmp < payloadsz;) {
		g_autoptr(FuFirmware) firmware_coswid = fu_coswid_firmware_new();
		g_autoptr(GBytes) fw2 = NULL;

		/* CBOR parse */
		fw2 = fu_bytes_new_offset(payload, offset_tmp, payloadsz - offset_tmp, error);
		if (fw2 == NULL)
			return FALSE;
		if (!fu_firmware_parse(firmware_coswid,
				       fw2,
				       flags | FWUPD_INSTALL_FLAG_NO_SEARCH,
				       error))
			return FALSE;
		fu_firmware_add_image(firmware, firmware_coswid);
		if (fu_firmware_get_size(firmware_coswid) == 0) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "coSWID read no bytes");
			return FALSE;
		}
		offset_tmp += fu_firmware_get_size(firmware_coswid);
	}

	/* success */
	return TRUE;
}

static guint8
fu_uswid_firmware_calculate_hdrsz(FuUswidFirmware *self)
{
	FuUswidFirmwarePrivate *priv = GET_PRIVATE(self);
	if (priv->hdrver == 0x1)
		return USWID_HEADER_SIZE_V1;
	return USWID_HEADER_SIZE_V2;
}

static GBytes *
fu_uswid_firmware_write(FuFirmware *firmware, GError **error)
{
	FuUswidFirmware *self = FU_USWID_FIRMWARE(firmware);
	FuUswidFirmwarePrivate *priv = GET_PRIVATE(self);
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GByteArray) payload = g_byte_array_new();
	g_autoptr(GBytes) payload_blob = NULL;
	g_autoptr(GPtrArray) images = fu_firmware_get_images(firmware);

	/* generate early so we know the size */
	for (guint i = 0; i < images->len; i++) {
		FuFirmware *firmware_coswid = g_ptr_array_index(images, i);
		g_autoptr(GBytes) fw = fu_firmware_write(firmware_coswid, error);
		if (fw == NULL)
			return NULL;
		fu_byte_array_append_bytes(payload, fw);
	}

	/* zlibify */
	if (priv->compressed) {
		g_autoptr(GConverter) conv = NULL;
		g_autoptr(GInputStream) istream1 = NULL;
		g_autoptr(GInputStream) istream2 = NULL;

		conv = G_CONVERTER(g_zlib_compressor_new(G_ZLIB_COMPRESSOR_FORMAT_ZLIB, -1));
		istream1 = g_memory_input_stream_new_from_data(payload->data, payload->len, NULL);
		istream2 = g_converter_input_stream_new(istream1, conv);
		payload_blob = fu_bytes_get_contents_stream(istream2, G_MAXSIZE, error);
		if (payload_blob == NULL)
			return NULL;
	} else {
		payload_blob = g_byte_array_free_to_bytes(g_steal_pointer(&payload));
	}

	/* header then CBOR blob */
	g_byte_array_append(buf, USWID_HEADER_MAGIC, sizeof(USWID_HEADER_MAGIC));
	fu_byte_array_append_uint8(buf, priv->hdrver);
	fu_byte_array_append_uint16(buf, fu_uswid_firmware_calculate_hdrsz(self), G_LITTLE_ENDIAN);
	fu_byte_array_append_uint32(buf, g_bytes_get_size(payload_blob), G_LITTLE_ENDIAN);
	if (priv->hdrver >= 2) {
		guint8 flags = 0;
		if (priv->compressed)
			flags |= USWID_HEADER_FLAG_COMPRESSED;
		fu_byte_array_append_uint8(buf, flags);
	}
	fu_byte_array_append_bytes(buf, payload_blob);

	/* success */
	return g_byte_array_free_to_bytes(g_steal_pointer(&buf));
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
	str = xb_node_query_text(n, "compressed", NULL);
	if (str != NULL) {
		if (!fu_strtobool(str, &priv->compressed, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_uswid_firmware_init(FuUswidFirmware *self)
{
	FuUswidFirmwarePrivate *priv = GET_PRIVATE(self);
	priv->hdrver = USWID_HEADER_VERSION_V1;
	priv->compressed = FALSE;
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_STORED_SIZE);
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_ALWAYS_SEARCH);
}

static void
fu_uswid_firmware_class_init(FuUswidFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->check_magic = fu_uswid_firmware_check_magic;
	klass_firmware->parse = fu_uswid_firmware_parse;
	klass_firmware->write = fu_uswid_firmware_write;
	klass_firmware->build = fu_uswid_firmware_build;
	klass_firmware->export = fu_uswid_firmware_export;
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
