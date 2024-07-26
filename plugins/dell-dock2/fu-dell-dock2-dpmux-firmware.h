/*
 * Copyright 2024 Dell Technologies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_DELL_DOCK2_DPMUX_FIRMWARE (fu_dell_dock2_dpmux_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuDellDock2DpmuxFirmware,
		     fu_dell_dock2_dpmux_firmware,
		     FU,
		     DELL_DOCK2_DPMUX_FIRMWARE,
		     FuFirmware)

FuFirmware *
fu_dell_dock2_dpmux_firmware_new(void);
