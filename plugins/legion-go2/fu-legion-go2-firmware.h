/*
 * Copyright 2025 hya1711 <591770796@qq.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_LEGION_GO2_FIRMWARE (fu_legion_go2_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuLegionGo2Firmware,
		     fu_legion_go2_firmware,
		     FU,
		     LEGION_GO2_FIRMWARE,
		     FuFirmware)

FuFirmware *
fu_legion_go2_firmware_new(void);
