/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2020 Mario Limonciello <mario.limonciello@dell.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_THUNDERBOLT_DEVICE (fu_thunderbolt_device_get_type ())
G_DECLARE_FINAL_TYPE (FuThunderboltDevice, fu_thunderbolt_device, FU, THUNDERBOLT_DEVICE, FuUdevDevice)
