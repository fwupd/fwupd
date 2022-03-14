/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuFirmware"

#include "config.h"

#include "fu-common.h"
#include "fu-uswid-firmware.h"

/**
 * FuUswidFirmware:
 *
 * A uSWID header on a single coSWID CBOR section.
 *
 * See also: [class@FuCoswidFirmware]
 */

typedef struct {
	guint8 hdrver;
} FuUswidFirmwarePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuUswidFirmware, fu_uswid_firmware, FU_TYPE_COSWID_FIRMWARE)
#define GET_PRIVATE(o) (fu_uswid_firmware_get_instance_private(o))

#define USWID_HEADER_VERSION 1
#define USWID_HEADER_SIZE    23

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
}

static gboolean
fu_uswid_firmware_parse(FuFirmware *firmware,
			GBytes *fw,
			guint64 addr_start,
			guint64 addr_end,
			FwupdInstallFlags flags,
			GError **error)
{
	FuUswidFirmware *self = FU_USWID_FIRMWARE(firmware);
	FuUswidFirmwarePrivate *priv = GET_PRIVATE(self);
	gsize offset = 0;
	guint16 hdrsz = 0;
	guint32 payloadsz = 0;
	gsize bufsz;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);
	g_autoptr(GBytes) fw2 = NULL;

	/* find start */
	if (!fu_memmem_safe(g_bytes_get_data(fw, NULL),
			    g_bytes_get_size(fw), /* haystack */
			    USWID_HEADER_MAGIC,
			    sizeof(USWID_HEADER_MAGIC),
			    &offset,
			    error))
		return FALSE;
	fu_firmware_set_offset(firmware, offset);
	offset += sizeof(USWID_HEADER_MAGIC);

	/* hdrver */
	if (!fu_common_read_uint8_safe(buf, bufsz, offset, &priv->hdrver, error))
		return FALSE;
	if (priv->hdrver < USWID_HEADER_VERSION) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "header version was unsupported");
		return FALSE;
	}
	offset += sizeof(guint8);

	/* hdrsz */
	if (!fu_common_read_uint16_safe(buf, bufsz, offset, &hdrsz, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (hdrsz < USWID_HEADER_SIZE) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "header size is invalid");
		return FALSE;
	}
	offset += sizeof(guint16);

	/* payloadsz */
	if (!fu_common_read_uint32_safe(buf, bufsz, offset, &payloadsz, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (payloadsz == 0x0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "payload size is invalid");
		return FALSE;
	}
	offset += sizeof(guint32);

	/* payload */
	fw2 = fu_common_bytes_new_offset(fw, offset, payloadsz, error);
	if (fw2 == NULL)
		return FALSE;

	/* CBOR parse */
	return FU_FIRMWARE_CLASS(fu_uswid_firmware_parent_class)
	    ->parse(firmware, fw2, addr_start, addr_end, flags, error);
}

static GBytes *
fu_uswid_firmware_write(FuFirmware *firmware, GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GBytes) fw = NULL;

	/* generate early so we know the size */
	fw = FU_FIRMWARE_CLASS(fu_uswid_firmware_parent_class)->write(firmware, error);
	if (fw == NULL)
		return NULL;

	/* header then CBOR blob */
	g_byte_array_append(buf, USWID_HEADER_MAGIC, sizeof(USWID_HEADER_MAGIC));
	fu_byte_array_append_uint8(buf, USWID_HEADER_VERSION);
	fu_byte_array_append_uint16(buf, USWID_HEADER_SIZE, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint32(buf, g_bytes_get_size(fw), G_LITTLE_ENDIAN);
	fu_byte_array_append_bytes(buf, fw);

	/* success */
	return g_byte_array_free_to_bytes(g_steal_pointer(&buf));
}

static gboolean
fu_uswid_firmware_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuUswidFirmware *self = FU_USWID_FIRMWARE(firmware);
	FuUswidFirmwarePrivate *priv = GET_PRIVATE(self);
	guint64 tmp;

	/* simple properties */
	tmp = xb_node_query_text_as_uint(n, "hdrver", NULL);
	if (tmp != G_MAXUINT64 && tmp <= G_MAXUINT8)
		priv->hdrver = tmp;

	/* success */
	return TRUE;
}

static void
fu_uswid_firmware_init(FuUswidFirmware *self)
{
	FuUswidFirmwarePrivate *priv = GET_PRIVATE(self);
	priv->hdrver = USWID_HEADER_VERSION;
}

static void
fu_uswid_firmware_class_init(FuUswidFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
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
