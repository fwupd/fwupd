/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2021 Jason Gerecke <killertofu@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-wac-module.h"

#define FU_TYPE_WAC_MODULE_BLUETOOTH_ID6 (fu_wac_module_bluetooth_id6_get_type())
G_DECLARE_FINAL_TYPE(FuWacModuleBluetoothId6,
		     fu_wac_module_bluetooth_id6,
		     FU,
		     WAC_MODULE_BLUETOOTH_ID6,
		     FuWacModule)

FuWacModule *
fu_wac_module_bluetooth_id6_new(FuDevice *proxy);
