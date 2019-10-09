/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-wac-module.h"

#define FU_TYPE_WAC_MODULE_TOUCH (fu_wac_module_touch_get_type ())
G_DECLARE_FINAL_TYPE (FuWacModuleTouch, fu_wac_module_touch, FU, WAC_MODULE_TOUCH, FuWacModule)

FuWacModule	*fu_wac_module_touch_new	(GUsbDevice	*usb_device);
