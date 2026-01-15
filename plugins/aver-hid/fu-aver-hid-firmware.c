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
fu_aver_hid_firmware_parse(FuFirmware *firmware,
			   GInputStream *stream,
			   FuFirmwareParseFlags flags,
			   GError **error)
{
	g_autoptr(FuFirmware) archive = fu_zip_archive_new();
	g_autoptr(GPtrArray) imgs = NULL;

	if (!fu_firmware_parse_stream(archive, stream, 0x0, FU_FIRMWARE_PARSE_FLAG_NONE, error))
		return FALSE;
	imgs = fu_firmware_get_images(archive);
	for (guint i = 0; i < imgs->len; i++) {
		FuFirmware *img = g_ptr_array_index(imgs, i);
		const gchar *filename = fu_firmware_get_id(img);
		if (g_str_has_suffix(filename, ".dat")) {
			g_autofree gchar *version = g_strndup(filename, strlen(filename) - 4);
			fu_firmware_set_version(firmware, version);
			fu_firmware_set_filename(firmware, filename);
		}
	}
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
