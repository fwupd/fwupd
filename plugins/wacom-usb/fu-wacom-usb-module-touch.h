/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-wacom-usb-module.h"

#define FU_TYPE_WACOM_USB_MODULE_TOUCH (fu_wacom_usb_module_touch_get_type())
G_DECLARE_FINAL_TYPE(FuWacomUsbModuleTouch,
		     fu_wacom_usb_module_touch,
		     FU,
		     WACOM_USB_MODULE_TOUCH,
		     FuWacomUsbModule)

FuWacomUsbModule *
fu_wacom_usb_module_touch_new(FuDevice *proxy);
