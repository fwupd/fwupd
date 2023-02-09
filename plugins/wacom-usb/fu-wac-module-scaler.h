/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2021 Jason Gerecke <killertofu@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-wac-module.h"

#define FU_TYPE_WAC_MODULE_SCALER (fu_wac_module_scaler_get_type())
G_DECLARE_FINAL_TYPE(FuWacModuleScaler, fu_wac_module_scaler, FU, WAC_MODULE_SCALER, FuWacModule)

FuWacModule *
fu_wac_module_scaler_new(FuDevice *proxy);
