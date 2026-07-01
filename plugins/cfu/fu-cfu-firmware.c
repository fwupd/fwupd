/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-cfu-firmware.h"

struct _FuCfuFirmware {
	FuFirmware parent_instance;
};

G_DEFINE_TYPE(FuCfuFirmware, fu_cfu_firmware, FU_TYPE_FIRMWARE)

static void
fu_cfu_firmware_init(FuCfuFirmware *self)
{
}

static void
fu_cfu_firmware_class_init(FuCfuFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	fu_firmware_add_image_gtype(firmware_class, FU_TYPE_CFU_OFFER);
	fu_firmware_add_image_gtype(firmware_class, FU_TYPE_CFU_PAYLOAD);
}

FuFirmware *
fu_cfu_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_CFU_FIRMWARE, NULL));
}
