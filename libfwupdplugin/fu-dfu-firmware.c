/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuFirmware"

#include "config.h"

#include <string.h>

#include "fu-byte-array.h"
#include "fu-bytes.h"
#include "fu-common.h"
#include "fu-crc.h"
#include "fu-dfu-firmware-private.h"
#include "fu-mem.h"

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

typedef struct __attribute__((packed)) {
	guint16 release;
	guint16 pid;
	guint16 vid;
	guint16 ver;
	guint8 sig[3];
	guint8 len;
	guint32 crc;
} FuDfuFirmwareFooter;

static gboolean
fu_dfu_firmware_check_magic(FuFirmware *firmware, GBytes *fw, gsize offset, GError **error)
{
	guint8 magic[3] = {0x0};

	/* is a footer */
	if (!fu_memcpy_safe(magic,
			    sizeof(magic),
			    0x0, /* dst */
			    g_bytes_get_data(fw, NULL),
			    g_bytes_get_size(fw),
			    g_bytes_get_size(fw) - G_STRUCT_OFFSET(FuDfuFirmwareFooter, sig),
			    sizeof(magic),
			    error))
		return FALSE;
	if (memcmp(magic, "UFD", sizeof(magic)) != 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "no DFU signature");
		return FALSE;
	}

	/* success */
	return TRUE;
}

gboolean
fu_dfu_firmware_parse_footer(FuDfuFirmware *self,
			     GBytes *fw,
			     FwupdInstallFlags flags,
			     GError **error)
{
	FuDfuFirmwarePrivate *priv = GET_PRIVATE(self);
	FuDfuFirmwareFooter ftr;
	gsize len;
	guint32 crc;
	guint32 crc_new;
	const guint8 *data = g_bytes_get_data(fw, &len);

	/* verify the checksum */
	if (!fu_memcpy_safe((guint8 *)&ftr,
			    sizeof(FuDfuFirmwareFooter),
			    0x0, /* dst */
			    data,
			    len,
			    len - sizeof(FuDfuFirmwareFooter), /* src */
			    sizeof(FuDfuFirmwareFooter),
			    error)) {
		g_prefix_error(error, "failed to read magic: ");
		return FALSE;
	}
	crc = GUINT32_FROM_LE(ftr.crc);
	if ((flags & FWUPD_INSTALL_FLAG_IGNORE_CHECKSUM) == 0) {
		crc_new = ~fu_crc32(data, len - 4);
		if (crc != crc_new) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "CRC failed, expected %04x, got %04x",
				    crc_new,
				    GUINT32_FROM_LE(ftr.crc));
			return FALSE;
		}
	}

	/* set from footer */
	priv->vid = GUINT16_FROM_LE(ftr.vid);
	priv->pid = GUINT16_FROM_LE(ftr.pid);
	priv->release = GUINT16_FROM_LE(ftr.release);
	priv->dfu_version = GUINT16_FROM_LE(ftr.ver);
	priv->footer_len = ftr.len;

	/* check reported length */
	if (priv->footer_len > len) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "reported footer size %04x larger than file %04x",
			    (guint)priv->footer_len,
			    (guint)len);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_dfu_firmware_parse(FuFirmware *firmware,
		      GBytes *fw,
		      gsize offset,
		      FwupdInstallFlags flags,
		      GError **error)
{
	FuDfuFirmware *self = FU_DFU_FIRMWARE(firmware);
	FuDfuFirmwarePrivate *priv = GET_PRIVATE(self);
	gsize len = g_bytes_get_size(fw);
	g_autoptr(GBytes) contents = NULL;

	/* parse footer */
	if (!fu_dfu_firmware_parse_footer(self, fw, flags, error))
		return FALSE;

	/* trim footer off */
	contents = fu_bytes_new_offset(fw, 0, len - priv->footer_len, error);
	if (contents == NULL)
		return FALSE;
	fu_firmware_set_bytes(firmware, contents);
	return TRUE;
}

GBytes *
fu_dfu_firmware_append_footer(FuDfuFirmware *self, GBytes *contents, GError **error)
{
	FuDfuFirmwarePrivate *priv = GET_PRIVATE(self);
	GByteArray *buf = g_byte_array_new();
	const guint8 *blob;
	gsize blobsz = 0;

	/* add the raw firmware data */
	blob = g_bytes_get_data(contents, &blobsz);
	g_byte_array_append(buf, blob, blobsz);

	/* append footer */
	fu_byte_array_append_uint16(buf, priv->release, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint16(buf, priv->pid, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint16(buf, priv->vid, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint16(buf, priv->dfu_version, G_LITTLE_ENDIAN);
	g_byte_array_append(buf, (const guint8 *)"UFD", 3);
	fu_byte_array_append_uint8(buf, sizeof(FuDfuFirmwareFooter));
	fu_byte_array_append_uint32(buf, ~fu_crc32(buf->data, buf->len), G_LITTLE_ENDIAN);
	return g_byte_array_free_to_bytes(buf);
}

static GBytes *
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
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->check_magic = fu_dfu_firmware_check_magic;
	klass_firmware->export = fu_dfu_firmware_export;
	klass_firmware->parse = fu_dfu_firmware_parse;
	klass_firmware->write = fu_dfu_firmware_write;
	klass_firmware->build = fu_dfu_firmware_build;
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
