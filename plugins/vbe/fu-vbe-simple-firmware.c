/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-vbe-simple-firmware.h"

struct _FuVbeSimpleFirmware {
	FuFirmware parent_instance;
};

G_DEFINE_TYPE(FuVbeSimpleFirmware, fu_vbe_simple_firmware, FU_TYPE_FIRMWARE)

static void
fu_vbe_simple_firmware_init(FuVbeSimpleFirmware *self)
{
}

static void
fu_vbe_simple_firmware_class_init(FuVbeSimpleFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	fu_firmware_add_image_gtype(firmware_class, FU_TYPE_FDT_IMAGE);
}

FuFirmware *
fu_vbe_simple_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_VBE_SIMPLE_FIRMWARE, NULL));
}
