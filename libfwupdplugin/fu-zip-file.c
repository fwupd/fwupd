/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuZipFile"

#include "config.h"

#include "fu-common.h"
#include "fu-zip-file.h"

typedef struct {
	FuZipCompression compression;
} FuZipFilePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuZipFile, fu_zip_file, FU_TYPE_FIRMWARE)
#define GET_PRIVATE(o) (fu_zip_file_get_instance_private(o))

/**
 * fu_zip_file_get_compression:
 * @self: a #FuZipFile
 *
 * Gets the archive compression type.
 *
 * Returns: a #FuZipCompression, e.g. #FU_ZIP_COMPRESSION_DEFLATE
 *
 * Since: 2.1.1
 **/
FuZipCompression
fu_zip_file_get_compression(FuZipFile *self)
{
	FuZipFilePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_ZIP_FILE(self), FU_ZIP_COMPRESSION_NONE);
	return priv->compression;
}

/**
 * fu_zip_file_set_compression:
 * @self: a #FuZipFile
 * @compression: a #FuZipCompression, e.g. #FU_ZIP_COMPRESSION_DEFLATE
 *
 * Sets the archive compression type.
 *
 * Since: 2.1.1
 **/
void
fu_zip_file_set_compression(FuZipFile *self, FuZipCompression compression)
{
	FuZipFilePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_ZIP_FILE(self));
	priv->compression = compression;
}

static gboolean
fu_zip_file_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuZipFile *self = FU_ZIP_FILE(firmware);
	FuZipFilePrivate *priv = GET_PRIVATE(self);
	const gchar *tmp;

	/* simple properties */
	tmp = xb_node_query_text(n, "compression", NULL);
	if (tmp != NULL)
		priv->compression = fu_zip_compression_from_string(tmp);

	/* success */
	return TRUE;
}

static void
fu_zip_file_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuZipFile *self = FU_ZIP_FILE(firmware);
	FuZipFilePrivate *priv = GET_PRIVATE(self);
	fu_xmlb_builder_insert_kv(bn,
				  "compression",
				  fu_zip_compression_to_string(priv->compression));
}

static void
fu_zip_file_class_init(FuZipFileClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->build = fu_zip_file_build;
	firmware_class->export = fu_zip_file_export;
}

static void
fu_zip_file_init(FuZipFile *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_CHECKSUM);
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_STORED_SIZE);
}

/**
 * fu_zip_file_new:
 *
 * Returns: (transfer full): a #FuZipFile
 *
 * Since: 2.1.1
 **/
FuFirmware *
fu_zip_file_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_ZIP_FILE, NULL));
}
