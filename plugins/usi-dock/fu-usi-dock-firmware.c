/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-usi-dock-firmware.h"

struct _FuUsiDockFirmware {
	FuFirmware parent_instance;
};

G_DEFINE_TYPE(FuUsiDockFirmware, fu_usi_dock_firmware, FU_TYPE_FIRMWARE)

static void
fu_usi_dock_firmware_init(FuUsiDockFirmware *self)
{
}

static void
fu_usi_dock_firmware_class_init(FuUsiDockFirmwareClass *klass)
{
}

FuFirmware *
fu_usi_dock_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_USI_DOCK_FIRMWARE, NULL));
}
