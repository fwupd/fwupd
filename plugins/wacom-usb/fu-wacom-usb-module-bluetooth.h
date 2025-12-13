/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-wacom-usb-module.h"

#define FU_TYPE_WACOM_USB_MODULE_BLUETOOTH (fu_wacom_usb_module_bluetooth_get_type())
G_DECLARE_FINAL_TYPE(FuWacomUsbModuleBluetooth,
		     fu_wacom_usb_module_bluetooth,
		     FU,
		     WACOM_USB_MODULE_BLUETOOTH,
		     FuWacomUsbModule)

FuWacomUsbModule *
fu_wacom_usb_module_bluetooth_new(FuDevice *proxy);
