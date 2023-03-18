/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2023 Joshua Dickens <joshua.dickens@wacom.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-wac-module.h"

#define FU_TYPE_WAC_MODULE_TOUCH_ID7 (fu_wac_module_touch_id7_get_type())
G_DECLARE_FINAL_TYPE(FuWacModuleTouchId7, fu_wac_module_touch_id7, FU, WAC_MODULE_TOUCH_ID7, FuWacModule)
#define FU_WAC_MODULE_CHUNK_SIZE  128

FuWacModule *
fu_wac_module_touch_id7_new(FuDevice *proxy);
