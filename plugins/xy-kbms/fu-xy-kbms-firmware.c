/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-xy-kbms-firmware.h"

/* the image is encrypted and has no header, so all we can check is the size */
#define FU_XY_KBMS_FIRMWARE_SIZE_MAX 0x20000
#define FU_XY_KBMS_FIRMWARE_ALIGN    0x800

struct _FuXyKbmsFirmware {
	FuFirmware parent_instance;
};

G_DEFINE_TYPE(FuXyKbmsFirmware, fu_xy_kbms_firmware, FU_TYPE_FIRMWARE)

static gboolean
fu_xy_kbms_firmware_parse(FuFirmware *firmware,
			  FuInputStream *stream,
			  FuFirmwareParseFlags flags,
			  GError **error)
{
	gsize streamsz = 0;

	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	if (streamsz == 0 || streamsz >= FU_XY_KBMS_FIRMWARE_SIZE_MAX) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "firmware size 0x%zx invalid, must be below 0x%x",
			    streamsz,
			    (guint)FU_XY_KBMS_FIRMWARE_SIZE_MAX);
		return FALSE;
	}
	if (streamsz % FU_XY_KBMS_FIRMWARE_ALIGN != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "firmware size 0x%zx invalid, must be a multiple of 0x%x",
			    streamsz,
			    (guint)FU_XY_KBMS_FIRMWARE_ALIGN);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_xy_kbms_firmware_init(FuXyKbmsFirmware *self)
{
}

static void
fu_xy_kbms_firmware_class_init(FuXyKbmsFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	fu_firmware_set_size_max(firmware_class, FU_XY_KBMS_FIRMWARE_SIZE_MAX);
	firmware_class->parse = fu_xy_kbms_firmware_parse;
}

FuFirmware *
fu_xy_kbms_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_XY_KBMS_FIRMWARE, NULL));
}
