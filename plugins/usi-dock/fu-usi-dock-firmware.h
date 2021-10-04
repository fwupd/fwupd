/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_USI_DOCK_FIRMWARE (fu_usi_dock_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuUsiDockFirmware, fu_usi_dock_firmware, FU, USI_DOCK_FIRMWARE, FuFirmware)

FuFirmware *
fu_usi_dock_firmware_new(void);
