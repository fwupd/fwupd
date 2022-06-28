/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2021-2023 Jason Gerecke <jason.gerecke@wacom.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-wac-module.h"

#define FU_TYPE_WAC_MODULE_BLUETOOTH_ID9 (fu_wac_module_bluetooth_id9_get_type())
G_DECLARE_FINAL_TYPE(FuWacModuleBluetoothId9,
		     fu_wac_module_bluetooth_id9,
		     FU,
		     WAC_MODULE_BLUETOOTH_ID9,
		     FuWacModule)

FuWacModule *
fu_wac_module_bluetooth_id9_new(FuDevice *proxy);
