/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-plugin.h"

#define FU_TYPE_OPTIONROM_DEVICE (fu_optionrom_device_get_type ())
G_DECLARE_FINAL_TYPE (FuOptionromDevice, fu_optionrom_device, FU, OPTIONROM_DEVICE, FuUdevDevice)
