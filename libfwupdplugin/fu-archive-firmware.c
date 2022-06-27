/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2022 Gaël PORTAY <gael.portay@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuFirmware"

#include "config.h"

#include "fu-archive-firmware.h"
#include "fu-archive.h"
#include "fu-common.h"

/**
 * FuArchiveFirmware:
 *
 * An archive firmware image, typically for nested firmware volumes.
 *
 * See also: [class@FuFirmware]
 */

typedef struct {
	FuArchiveFormat format;
	FuArchiveCompression compression;
} FuArchiveFirmwarePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuArchiveFirmware, fu_archive_firmware, FU_TYPE_FIRMWARE)
#define GET_PRIVATE(o) (fu_archive_firmware_get_instance_private(o))

static void
fu_archive_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuArchiveFirmware *self = FU_ARCHIVE_FIRMWARE(firmware);
	FuArchiveFirmwarePrivate *priv = GET_PRIVATE(self);
	fu_xmlb_builder_insert_kv(bn, "format", fu_archive_format_to_string(priv->format));
	fu_xmlb_builder_insert_kv(bn,
				  "compression",
				  fu_archive_compression_to_string(priv->compression));
}

static gboolean
fu_archive_firmware_parse_cb(FuArchive *self,
			     const gchar *filename,
			     GBytes *bytes,
			     gpointer user_data,
			     GError **error)
{
	FuFirmware *firmware = FU_FIRMWARE(user_data);
	g_autoptr(FuFirmware) img = fu_firmware_new_from_bytes(bytes);
	fu_firmware_set_id(img, filename);
	fu_firmware_add_image(firmware, img);
	return TRUE;
}

static gboolean
fu_archive_firmware_parse(FuFirmware *firmware,
			  GBytes *fw,
			  gsize offset,
			  FwupdInstallFlags flags,
			  GError **error)
{
	g_autoptr(FuArchive) archive = NULL;

	/* load archive */
	archive = fu_archive_new(fw, FU_ARCHIVE_FLAG_IGNORE_PATH, error);
	if (archive == NULL)
		return FALSE;

	/* decompress each image in the archive */
	return fu_archive_iterate(archive, fu_archive_firmware_parse_cb, firmware, error);
}

/**
 * fu_archive_firmware_get_format:
 * @self: a #FuArchiveFirmware
 *
 * Gets the archive format.
 *
 * Returns: format
 *
 * Since: 1.8.1
 **/
FuArchiveFormat
fu_archive_firmware_get_format(FuArchiveFirmware *self)
{
	FuArchiveFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_ARCHIVE_FIRMWARE(self), FU_ARCHIVE_FORMAT_UNKNOWN);
	return priv->format;
}

/**
 * fu_archive_firmware_set_format:
 * @self: a #FuArchiveFirmware
 * @format: the archive format, e.g. %FU_ARCHIVE_FORMAT_ZIP
 *
 * Sets the archive format.
 *
 * Since: 1.8.1
 **/
void
fu_archive_firmware_set_format(FuArchiveFirmware *self, FuArchiveFormat format)
{
	FuArchiveFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_ARCHIVE_FIRMWARE(self));
	priv->format = format;
}

/**
 * fu_archive_firmware_get_compression:
 * @self: A #FuArchiveFirmware
 *
 * Returns the compression.
 *
 * Returns: compression
 *
 * Since: 1.8.1
 **/
FuArchiveCompression
fu_archive_firmware_get_compression(FuArchiveFirmware *self)
{
	FuArchiveFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_ARCHIVE_FIRMWARE(self), FU_ARCHIVE_COMPRESSION_UNKNOWN);
	return priv->compression;
}

/**
 * fu_archive_firmware_set_compression:
 * @self: A #FuArchiveFirmware
 * @compression: the compression, e.g. %FU_ARCHIVE_COMPRESSION_NONE
 *
 * Sets the compression.
 *
 * Since: 1.8.1
 **/
void
fu_archive_firmware_set_compression(FuArchiveFirmware *self, FuArchiveCompression compression)
{
	FuArchiveFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_ARCHIVE_FIRMWARE(self));
	priv->compression = compression;
}

static GBytes *
fu_archive_firmware_write(FuFirmware *firmware, GError **error)
{
	FuArchiveFirmware *self = FU_ARCHIVE_FIRMWARE(firmware);
	FuArchiveFirmwarePrivate *priv = GET_PRIVATE(self);
	g_autoptr(FuArchive) archive = NULL;
	g_autoptr(GPtrArray) images = fu_firmware_get_images(firmware);

	/* sanity check */
	if (priv->format == FU_ARCHIVE_FORMAT_UNKNOWN) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "firmware archive format unspecified");
		return NULL;
	}
	if (priv->compression == FU_ARCHIVE_COMPRESSION_UNKNOWN) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "firmware archive compression unspecified");
		return NULL;
	}

	/* save archive and compress each image to the archive */
	archive = fu_archive_new(NULL, FU_ARCHIVE_FLAG_NONE, NULL);
	for (guint i = 0; i < images->len; i++) {
		FuFirmware *img = g_ptr_array_index(images, i);
		g_autoptr(GBytes) blob = NULL;

		if (fu_firmware_get_id(img) == NULL) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "image has no ID");
			return NULL;
		}
		blob = fu_firmware_get_bytes(img, error);
		if (blob == NULL)
			return NULL;
		fu_archive_add_entry(archive, fu_firmware_get_id(img), blob);
	}
	return fu_archive_write(archive, priv->format, priv->compression, error);
}

static gboolean
fu_archive_firmware_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuArchiveFirmware *self = FU_ARCHIVE_FIRMWARE(firmware);
	const gchar *tmp;

	/* simple properties */
	tmp = xb_node_query_text(n, "format", NULL);
	if (tmp != NULL) {
		FuArchiveFormat format = fu_archive_format_from_string(tmp);
		if (format == FU_ARCHIVE_FORMAT_UNKNOWN) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "format %s not supported",
				    tmp);
			return FALSE;
		}
		fu_archive_firmware_set_format(self, format);
	}
	tmp = xb_node_query_text(n, "compression", NULL);
	if (tmp != NULL) {
		FuArchiveCompression compression = fu_archive_compression_from_string(tmp);
		if (compression == FU_ARCHIVE_COMPRESSION_UNKNOWN) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "compression %s not supported",
				    tmp);
			return FALSE;
		}
		fu_archive_firmware_set_compression(self, compression);
	}

	/* success */
	return TRUE;
}

static void
fu_archive_firmware_init(FuArchiveFirmware *self)
{
}

static void
fu_archive_firmware_class_init(FuArchiveFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->parse = fu_archive_firmware_parse;
	klass_firmware->write = fu_archive_firmware_write;
	klass_firmware->build = fu_archive_firmware_build;
	klass_firmware->export = fu_archive_firmware_export;
}

/**
 * fu_archive_firmware_new:
 *
 * Creates a new archive #FuFirmware
 *
 * Since: 1.7.3
 **/
FuFirmware *
fu_archive_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_ARCHIVE_FIRMWARE, NULL));
}
