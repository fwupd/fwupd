/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 * Copyright 2023 Joshua Dickens <joshua.dickens@wacom.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-wacom-usb-module.h"

#define FU_TYPE_WACOM_USB_MODULE_TOUCH_ID7 (fu_wacom_usb_module_touch_id7_get_type())
G_DECLARE_FINAL_TYPE(FuWacomUsbModuleTouchId7,
		     fu_wacom_usb_module_touch_id7,
		     FU,
		     WACOM_USB_MODULE_TOUCH_ID7,
		     FuWacomUsbModule)
#define FU_WACOM_USB_MODULE_CHUNK_SIZE 128

FuWacomUsbModule *
fu_wacom_usb_module_touch_id7_new(FuDevice *proxy);
