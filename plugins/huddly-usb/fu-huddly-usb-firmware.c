/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-huddly-usb-firmware.h"

struct _FuHuddlyUsbFirmware {
	FuFirmware parent_instance;
};

G_DEFINE_TYPE(FuHuddlyUsbFirmware, fu_huddly_usb_firmware, FU_TYPE_FIRMWARE)

static void
fu_huddly_usb_firmware_init(FuHuddlyUsbFirmware *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_NO_AUTO_DETECTION);
}

static void
fu_huddly_usb_firmware_class_init(FuHuddlyUsbFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	fu_firmware_set_size_max(firmware_class, 512 * FU_MB);
}
