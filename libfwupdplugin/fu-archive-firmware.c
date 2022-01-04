/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuFirmware"

#include "config.h"

#include "fu-archive-firmware.h"
#include "fu-archive.h"

/**
 * FuArchiveFirmware:
 *
 * An archive firmware image, typically for nested firmware volumes.
 *
 * See also: [class@FuFirmware]
 */

G_DEFINE_TYPE(FuArchiveFirmware, fu_archive_firmware, FU_TYPE_FIRMWARE)

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
			  guint64 addr_start,
			  guint64 addr_end,
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

static void
fu_archive_firmware_init(FuArchiveFirmware *self)
{
}

static void
fu_archive_firmware_class_init(FuArchiveFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->parse = fu_archive_firmware_parse;
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
