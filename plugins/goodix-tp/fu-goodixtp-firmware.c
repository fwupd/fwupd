/*
 * Copyright 2023 Goodix.inc <xulinkun@goodix.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-goodixtp-firmware.h"

typedef struct {
	FuFirmwareClass parent_instance;
} FuGoodixtpFirmwarePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuGoodixtpFirmware, fu_goodixtp_firmware, FU_TYPE_FIRMWARE)
#define GET_PRIVATE(o) (fu_goodixtp_firmware_get_instance_private(o))

static gchar *
fu_goodixtp_firmware_convert_version(FuFirmware *firmware, guint64 version_raw)
{
	return fu_version_from_uint64(version_raw, fu_firmware_get_version_format(firmware));
}

static void
fu_goodixtp_firmware_init(FuGoodixtpFirmware *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_NO_AUTO_DETECTION);
	fu_firmware_set_version_format(FU_FIRMWARE(self), FWUPD_VERSION_FORMAT_PAIR);
}

static void
fu_goodixtp_firmware_class_init(FuGoodixtpFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->convert_version = fu_goodixtp_firmware_convert_version;
}
