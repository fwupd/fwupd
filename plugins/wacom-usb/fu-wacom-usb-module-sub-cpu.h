/*
 * Copyright 2024 Jason Gerecke <jason.gerecke@wacom.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-wacom-usb-module.h"

#define FU_TYPE_WACOM_USB_MODULE_SUB_CPU (fu_wacom_usb_module_sub_cpu_get_type())
G_DECLARE_FINAL_TYPE(FuWacomUsbModuleSubCpu,
		     fu_wacom_usb_module_sub_cpu,
		     FU,
		     WACOM_USB_MODULE_SUB_CPU,
		     FuWacomUsbModule)

FuWacomUsbModule *
fu_wacom_usb_module_sub_cpu_new(FuDevice *proxy);
