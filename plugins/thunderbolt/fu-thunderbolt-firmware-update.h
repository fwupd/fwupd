/*
 * Copyright (C) 2020 Mario Limonciello <mario.limonciello@dell.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_THUNDERBOLT_FIRMWARE_UPDATE (fu_thunderbolt_firmware_update_get_type ())
G_DECLARE_FINAL_TYPE (FuThunderboltFirmwareUpdate, fu_thunderbolt_firmware_update, FU,THUNDERBOLT_FIRMWARE_UPDATE, FuThunderboltFirmware)

FuThunderboltFirmwareUpdate *fu_thunderbolt_firmware_update_new	(void);
