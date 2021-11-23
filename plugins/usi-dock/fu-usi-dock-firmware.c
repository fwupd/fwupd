/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-usi-dock-firmware.h"

struct _FuUSIDockFirmware {
	FuFirmware parent_instance;
};

G_DEFINE_TYPE(FuUSIDockFirmware, fu_usi_dock_firmware, FU_TYPE_FIRMWARE)

static void
fu_usi_dock_firmware_init(FuUSIDockFirmware *self)
{
}

static void
fu_usi_dock_firmware_class_init(FuUSIDockFirmwareClass *klass)
{
}

FuFirmware *
fu_usi_dock_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_USI_DOCK_FIRMWARE, NULL));
}
