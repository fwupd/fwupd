/*
 * Copyright 2026 Marc-Antoine Perennou <ma.perennou@criteo.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-redfish-firmware.h"

struct _FuRedfishFirmware {
	FuFirmware parent_instance;
};

G_DEFINE_TYPE(FuRedfishFirmware, fu_redfish_firmware, FU_TYPE_FIRMWARE)

static void
fu_redfish_firmware_init(FuRedfishFirmware *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_NO_AUTO_DETECTION);
}

static void
fu_redfish_firmware_class_init(FuRedfishFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	fu_firmware_set_size_max(firmware_class, 512 * FU_MB);
}
