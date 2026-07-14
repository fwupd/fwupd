/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_WISTRON_DOCK_FIRMWARE (fu_wistron_dock_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuWistronDockFirmware,
		     fu_wistron_dock_firmware,
		     FU,
		     WISTRON_DOCK_FIRMWARE,
		     FuFirmware)

FuFirmware *
fu_wistron_dock_firmware_new(void);
