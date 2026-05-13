/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 * Copyright 2021-2023 Jason Gerecke <jason.gerecke@wacom.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-wacom-usb-module.h"

#define FU_TYPE_WACOM_USB_MODULE_BLUETOOTH_ID9 (fu_wacom_usb_module_bluetooth_id9_get_type())
G_DECLARE_FINAL_TYPE(FuWacomUsbModuleBluetoothId9,
		     fu_wacom_usb_module_bluetooth_id9,
		     FU,
		     WACOM_USB_MODULE_BLUETOOTH_ID9,
		     FuWacomUsbModule)

FuWacomUsbModule *
fu_wacom_usb_module_bluetooth_id9_new(FuDevice *proxy);
