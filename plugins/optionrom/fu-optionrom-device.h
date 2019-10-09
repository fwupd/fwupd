/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-plugin.h"

G_BEGIN_DECLS

#define FU_TYPE_OPTIONROM_DEVICE (fu_optionrom_device_get_type ())
G_DECLARE_FINAL_TYPE (FuOptionromDevice, fu_optionrom_device, FU, OPTIONROM_DEVICE, FuUdevDevice)

FuOptionromDevice	*fu_optionrom_device_new			(FuUdevDevice	*device);

G_END_DECLS
