/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-wistron-dock-firmware.h"

struct _FuWistronDockFirmware {
	FuFirmware parent_instance;
};

G_DEFINE_TYPE(FuWistronDockFirmware, fu_wistron_dock_firmware, FU_TYPE_FIRMWARE)

static void
fu_wistron_dock_firmware_init(FuWistronDockFirmware *self)
{
}

static void
fu_wistron_dock_firmware_class_init(FuWistronDockFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	fu_firmware_add_image_gtype(firmware_class, FU_TYPE_ZIP_FILE);
}

FuFirmware *
fu_wistron_dock_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_WISTRON_DOCK_FIRMWARE, NULL));
}
