/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 * Copyright 2021 Jason Gerecke <killertofu@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-wacom-usb-module.h"

#define FU_TYPE_WACOM_USB_MODULE_SCALER (fu_wacom_usb_module_scaler_get_type())
G_DECLARE_FINAL_TYPE(FuWacomUsbModuleScaler,
		     fu_wacom_usb_module_scaler,
		     FU,
		     WACOM_USB_MODULE_SCALER,
		     FuWacomUsbModule)

FuWacomUsbModule *
fu_wacom_usb_module_scaler_new(FuDevice *proxy);
