/*
 * Copyright 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuFirmware"

#include "config.h"

#include <string.h>

#include "fu-byte-array.h"
#include "fu-bytes.h"
#include "fu-common.h"
#include "fu-crc.h"
#include "fu-dfu-firmware-private.h"
#include "fu-dfu-firmware-struct.h"
#include "fu-input-stream.h"

/**
 * FuDfuFirmware:
 *
 * A DFU firmware image.
 *
 * See also: [class@FuFirmware]
 */

typedef struct {
	guint16 vid;
	guint16 pid;
	guint16 release;
	guint16 dfu_version;
	guint8 footer_len;
} FuDfuFirmwarePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuDfuFirmware, fu_dfu_firmware, FU_TYPE_FIRMWARE)
#define GET_PRIVATE(o) (fu_dfu_firmware_get_instance_private(o))

static void
fu_dfu_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuDfuFirmware *self = FU_DFU_FIRMWARE(firmware);
	FuDfuFirmwarePrivate *priv = GET_PRIVATE(self);
	fu_xmlb_builder_insert_kx(bn, "vendor", priv->vid);
	fu_xmlb_builder_insert_kx(bn, "product", priv->pid);
	fu_xmlb_builder_insert_kx(bn, "release", priv->release);
	fu_xmlb_builder_insert_kx(bn, "dfu_version", priv->dfu_version);
}

/* private */
guint8
fu_dfu_firmware_get_footer_len(FuDfuFirmware *self)
{
	FuDfuFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_DFU_FIRMWARE(self), 0x0);
	return priv->footer_len;
}

/**
 * fu_dfu_firmware_get_vid:
 * @self: a #FuDfuFirmware
 *
 * Gets the vendor ID, or 0xffff for no restriction.
 *
 * Returns: integer
 *
 * Since: 1.3.3
 **/
guint16
fu_dfu_firmware_get_vid(FuDfuFirmware *self)
{
	FuDfuFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_DFU_FIRMWARE(self), 0x0);
	return priv->vid;
}

/**
 * fu_dfu_firmware_get_pid:
 * @self: a #FuDfuFirmware
 *
 * Gets the product ID, or 0xffff for no restriction.
 *
 * Returns: integer
 *
 * Since: 1.3.3
 **/
guint16
fu_dfu_firmware_get_pid(FuDfuFirmware *self)
{
	FuDfuFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_DFU_FIRMWARE(self), 0x0);
	return priv->pid;
}

/**
 * fu_dfu_firmware_get_release:
 * @self: a #FuDfuFirmware
 *
 * Gets the device ID, or 0xffff for no restriction.
 *
 * Returns: integer
 *
 * Since: 1.3.3
 **/
guint16
fu_dfu_firmware_get_release(FuDfuFirmware *self)
{
	FuDfuFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_DFU_FIRMWARE(self), 0x0);
	return priv->release;
}

/**
 * fu_dfu_firmware_get_version:
 * @self: a #FuDfuFirmware
 *
 * Gets the file format version with is 0x0100 by default.
 *
 * Returns: integer
 *
 * Since: 1.3.3
 **/
guint16
fu_dfu_firmware_get_version(FuDfuFirmware *self)
{
	FuDfuFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_DFU_FIRMWARE(self), 0x0);
	return priv->dfu_version;
}

/**
 * fu_dfu_firmware_set_vid:
 * @self: a #FuDfuFirmware
 * @vid: vendor ID, or 0xffff if the firmware should match any vendor
 *
 * Sets the vendor ID.
 *
 * Since: 1.3.3
 **/
void
fu_dfu_firmware_set_vid(FuDfuFirmware *self, guint16 vid)
{
	FuDfuFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_DFU_FIRMWARE(self));
	priv->vid = vid;
}

/**
 * fu_dfu_firmware_set_pid:
 * @self: a #FuDfuFirmware
 * @pid: product ID, or 0xffff if the firmware should match any product
 *
 * Sets the product ID.
 *
 * Since: 1.3.3
 **/
void
fu_dfu_firmware_set_pid(FuDfuFirmware *self, guint16 pid)
{
	FuDfuFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_DFU_FIRMWARE(self));
	priv->pid = pid;
}

/**
 * fu_dfu_firmware_set_release:
 * @self: a #FuDfuFirmware
 * @release: release, or 0xffff if the firmware should match any release
 *
 * Sets the release for the dfu firmware.
 *
 * Since: 1.3.3
 **/
void
fu_dfu_firmware_set_release(FuDfuFirmware *self, guint16 release)
{
	FuDfuFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_DFU_FIRMWARE(self));
	priv->release = release;
}

/**
 * fu_dfu_firmware_set_version:
 * @self: a #FuDfuFirmware
 * @version: integer
 *
 * Sets the file format version.
 *
 * Since: 1.3.3
 **/
void
fu_dfu_firmware_set_version(FuDfuFirmware *self, guint16 version)
{
	FuDfuFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_DFU_FIRMWARE(self));
	priv->dfu_version = version;
}

static gboolean
fu_dfu_firmware_validate(FuFirmware *firmware, GInputStream *stream, gsize offset, GError **error)
{
	gsize streamsz = 0;
	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	if (streamsz < FU_STRUCT_DFU_FTR_SIZE) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "stream was too small");
		return FALSE;
	}
	return fu_struct_dfu_ftr_validate_stream(stream, streamsz - FU_STRUCT_DFU_FTR_SIZE, error);
}

gboolean
fu_dfu_firmware_parse_footer(FuDfuFirmware *self,
			     GInputStream *stream,
			     FwupdInstallFlags flags,
			     GError **error)
{
	FuDfuFirmwarePrivate *priv = GET_PRIVATE(self);
	gsize bufsz;
	const guint8 *buf;
	g_autoptr(GByteArray) st = NULL;
	g_autoptr(GBytes) fw = NULL;

	fw = fu_input_stream_read_bytes(stream, 0, G_MAXSIZE, error);
	if (fw == NULL)
		return FALSE;
	buf = g_bytes_get_data(fw, &bufsz);

	/* parse */
	st = fu_struct_dfu_ftr_parse_stream(stream, bufsz - FU_STRUCT_DFU_FTR_SIZE, error);
	if (st == NULL)
		return FALSE;
	priv->vid = fu_struct_dfu_ftr_get_vid(st);
	priv->pid = fu_struct_dfu_ftr_get_pid(st);
	priv->release = fu_struct_dfu_ftr_get_release(st);
	priv->dfu_version = fu_struct_dfu_ftr_get_ver(st);
	priv->footer_len = fu_struct_dfu_ftr_get_len(st);

	/* verify the checksum */
	if ((flags & FWUPD_INSTALL_FLAG_IGNORE_CHECKSUM) == 0) {
		guint32 crc_new = fu_crc32(FU_CRC_KIND_B32_JAMCRC, buf, bufsz - 4);
		if (fu_struct_dfu_ftr_get_crc(st) != crc_new) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "CRC failed, expected 0x%04x, got 0x%04x",
				    crc_new,
				    fu_struct_dfu_ftr_get_crc(st));
			return FALSE;
		}
	}

	/* check reported length */
	if (priv->footer_len > bufsz) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "reported footer size 0x%04x larger than file 0x%04x",
			    (guint)priv->footer_len,
			    (guint)bufsz);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_dfu_firmware_parse(FuFirmware *firmware,
		      GInputStream *stream,
		      gsize offset,
		      FwupdInstallFlags flags,
		      GError **error)
{
	FuDfuFirmware *self = FU_DFU_FIRMWARE(firmware);
	FuDfuFirmwarePrivate *priv = GET_PRIVATE(self);
	gsize streamsz = 0;
	g_autoptr(GBytes) contents = NULL;

	/* parse footer */
	if (!fu_dfu_firmware_parse_footer(self, stream, flags, error))
		return FALSE;

	/* trim footer off */
	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	contents = fu_input_stream_read_bytes(stream, 0, streamsz - priv->footer_len, error);
	if (contents == NULL)
		return FALSE;
	fu_firmware_set_bytes(firmware, contents);
	return TRUE;
}

GByteArray *
fu_dfu_firmware_append_footer(FuDfuFirmware *self, GBytes *contents, GError **error)
{
	FuDfuFirmwarePrivate *priv = GET_PRIVATE(self);
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GByteArray) st = fu_struct_dfu_ftr_new();

	/* add the raw firmware data, the footer-less-CRC, and only then the CRC */
	fu_byte_array_append_bytes(buf, contents);
	fu_struct_dfu_ftr_set_release(st, priv->release);
	fu_struct_dfu_ftr_set_pid(st, priv->pid);
	fu_struct_dfu_ftr_set_vid(st, priv->vid);
	fu_struct_dfu_ftr_set_ver(st, priv->dfu_version);
	g_byte_array_append(buf, st->data, st->len - sizeof(guint32));
	fu_byte_array_append_uint32(buf,
				    fu_crc32(FU_CRC_KIND_B32_JAMCRC, buf->data, buf->len),
				    G_LITTLE_ENDIAN);
	return g_steal_pointer(&buf);
}

static GByteArray *
fu_dfu_firmware_write(FuFirmware *firmware, GError **error)
{
	FuDfuFirmware *self = FU_DFU_FIRMWARE(firmware);
	g_autoptr(GPtrArray) images = fu_firmware_get_images(firmware);
	g_autoptr(GBytes) fw = NULL;

	/* can only contain one image */
	if (images->len > 1) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "DFU only supports writing one image");
		return NULL;
	}

	/* add footer */
	fw = fu_firmware_get_bytes_with_patches(firmware, error);
	if (fw == NULL)
		return NULL;
	return fu_dfu_firmware_append_footer(self, fw, error);
}

static gboolean
fu_dfu_firmware_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuDfuFirmware *self = FU_DFU_FIRMWARE(firmware);
	FuDfuFirmwarePrivate *priv = GET_PRIVATE(self);
	guint64 tmp;

	/* optional properties */
	tmp = xb_node_query_text_as_uint(n, "vendor", NULL);
	if (tmp != G_MAXUINT64 && tmp <= G_MAXUINT16)
		priv->vid = tmp;
	tmp = xb_node_query_text_as_uint(n, "product", NULL);
	if (tmp != G_MAXUINT64 && tmp <= G_MAXUINT16)
		priv->pid = tmp;
	tmp = xb_node_query_text_as_uint(n, "release", NULL);
	if (tmp != G_MAXUINT64 && tmp <= G_MAXUINT16)
		priv->release = tmp;
	tmp = xb_node_query_text_as_uint(n, "dfu_version", NULL);
	if (tmp != G_MAXUINT64 && tmp <= G_MAXUINT16)
		priv->dfu_version = tmp;

	/* success */
	return TRUE;
}

static void
fu_dfu_firmware_init(FuDfuFirmware *self)
{
	FuDfuFirmwarePrivate *priv = GET_PRIVATE(self);
	priv->vid = 0xffff;
	priv->pid = 0xffff;
	priv->release = 0xffff;
	priv->dfu_version = FU_DFU_FIRMARE_VERSION_DFU_1_0;
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_CHECKSUM);
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_VID_PID);
}

static void
fu_dfu_firmware_class_init(FuDfuFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->validate = fu_dfu_firmware_validate;
	firmware_class->export = fu_dfu_firmware_export;
	firmware_class->parse = fu_dfu_firmware_parse;
	firmware_class->write = fu_dfu_firmware_write;
	firmware_class->build = fu_dfu_firmware_build;
}

/**
 * fu_dfu_firmware_new:
 *
 * Creates a new #FuFirmware of sub type Dfu
 *
 * Since: 1.3.3
 **/
FuFirmware *
fu_dfu_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_DFU_FIRMWARE, NULL));
}
