/*
 * Copyright 2019 Mario Limonciello <mario.limonciello@dell.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_EMMC_DEVICE (fu_emmc_device_get_type())
G_DECLARE_FINAL_TYPE(FuEmmcDevice, fu_emmc_device, FU, EMMC_DEVICE, FuUdevDevice)
