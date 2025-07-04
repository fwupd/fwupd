/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-aver-hid-firmware.h"

struct _FuAverHidFirmware {
	FuFirmware parent_instance;
};

G_DEFINE_TYPE(FuAverHidFirmware, fu_aver_hid_firmware, FU_TYPE_FIRMWARE)

static gboolean
fu_aver_hid_firmware_parse_archive_cb(FuArchive *self,
				      const gchar *filename,
				      GBytes *bytes,
				      gpointer user_data,
				      GError **error)
{
	FuFirmware *firmware = FU_FIRMWARE(user_data);
	if (g_str_has_suffix(filename, ".dat")) {
		g_autofree gchar *version = g_strndup(filename, strlen(filename) - 4);
		fu_firmware_set_version(firmware, version);
		fu_firmware_set_filename(firmware, filename);
	}
	return TRUE;
}

static gboolean
fu_aver_hid_firmware_parse(FuFirmware *firmware,
			   GInputStream *stream,
			   FuFirmwareParseFlags flags,
			   GError **error)
{
	g_autoptr(FuArchive) archive = NULL;
	archive = fu_archive_new_stream(stream, FU_ARCHIVE_FLAG_NONE, error);
	if (archive == NULL)
		return FALSE;
	if (!fu_archive_iterate(archive, fu_aver_hid_firmware_parse_archive_cb, firmware, error))
		return FALSE;
	return TRUE;
}

static void
fu_aver_hid_firmware_init(FuAverHidFirmware *self)
{
}

static void
fu_aver_hid_firmware_class_init(FuAverHidFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_aver_hid_firmware_parse;
}

FuFirmware *
fu_aver_hid_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_AVER_HID_FIRMWARE, NULL));
}
