/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-lxstouch-firmware.h"
#include "fu-lxstouch-struct.h"

struct _FuLxstouchFirmware {
	FuFirmware parent_instance;
	guint32 fw_offset;
};

G_DEFINE_TYPE(FuLxstouchFirmware, fu_lxstouch_firmware, FU_TYPE_FIRMWARE)

static gboolean
fu_lxstouch_firmware_parse(FuFirmware *firmware,
			   GInputStream *stream,
			   FuFirmwareParseFlags flags,
			   GError **error)
{
	FuLxstouchFirmware *self = FU_LXSTOUCH_FIRMWARE(firmware);
	gsize streamsz = 0;

	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;

	/* Validate firmware size */
	if (streamsz != FU_LXSTOUCH_FW_SIZE_APP_ONLY && streamsz != FU_LXSTOUCH_FW_SIZE_BOOT_APP) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "invalid firmware size 0x%x, expected 0x%x or 0x%x",
			    (guint)streamsz,
			    (guint)FU_LXSTOUCH_FW_SIZE_APP_ONLY,
			    (guint)FU_LXSTOUCH_FW_SIZE_BOOT_APP);
		return FALSE;
	}

	/* Determine firmware type and offset */
	if (streamsz == FU_LXSTOUCH_FW_SIZE_APP_ONLY) {
		self->fw_offset = FU_LXSTOUCH_FW_OFFSET_APP_ONLY;
		g_debug("Application-only firmware detected (112KB), offset=0x%04x",
			self->fw_offset);
	} else if (streamsz == FU_LXSTOUCH_FW_SIZE_BOOT_APP) {
		self->fw_offset = 0x0;
		g_debug("Boot+Application firmware detected (128KB), offset=0x%04x",
			self->fw_offset);
	}

	/* success */
	return TRUE;
}

guint32
fu_lxstouch_firmware_get_offset(FuLxstouchFirmware *self)
{
	g_return_val_if_fail(FU_IS_LXSTOUCH_FIRMWARE(self), 0);
	return self->fw_offset;
}

static void
fu_lxstouch_firmware_init(FuLxstouchFirmware *self)
{
	fu_firmware_set_images_max(FU_FIRMWARE(self), 2048);
	self->fw_offset = 0;
}

static void
fu_lxstouch_firmware_class_init(FuLxstouchFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_lxstouch_firmware_parse;
}

FuFirmware *
fu_lxstouch_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_LXSTOUCH_FIRMWARE, NULL));
}
