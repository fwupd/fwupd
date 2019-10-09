/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-wac-module.h"

#define FU_TYPE_WAC_MODULE_BLUETOOTH (fu_wac_module_bluetooth_get_type ())
G_DECLARE_FINAL_TYPE (FuWacModuleBluetooth, fu_wac_module_bluetooth, FU, WAC_MODULE_BLUETOOTH, FuWacModule)

FuWacModule	*fu_wac_module_bluetooth_new	(GUsbDevice	*usb_device);
