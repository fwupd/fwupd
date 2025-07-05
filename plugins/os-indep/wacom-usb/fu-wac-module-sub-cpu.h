/*
 * Copyright 2024 Jason Gerecke <jason.gerecke@wacom.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-wac-module.h"

#define FU_TYPE_WAC_MODULE_SUB_CPU (fu_wac_module_sub_cpu_get_type())
G_DECLARE_FINAL_TYPE(FuWacModuleSubCpu, fu_wac_module_sub_cpu, FU, WAC_MODULE_SUB_CPU, FuWacModule)

FuWacModule *
fu_wac_module_sub_cpu_new(FuDevice *proxy);
