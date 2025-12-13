/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 * Copyright 2021 Jason Gerecke <killertofu@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-wacom-usb-module.h"

#define FU_TYPE_WACOM_USB_MODULE_BLUETOOTH_ID6 (fu_wacom_usb_module_bluetooth_id6_get_type())
G_DECLARE_FINAL_TYPE(FuWacomUsbModuleBluetoothId6,
		     fu_wacom_usb_module_bluetooth_id6,
		     FU,
		     WACOM_USB_MODULE_BLUETOOTH_ID6,
		     FuWacomUsbModule)

FuWacomUsbModule *
fu_wacom_usb_module_bluetooth_id6_new(FuDevice *proxy);
