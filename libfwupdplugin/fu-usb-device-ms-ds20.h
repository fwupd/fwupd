/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-usb-device-ds20.h"

#define FU_TYPE_USB_DEVICE_MS_DS20 (fu_usb_device_ms_ds20_get_type())
G_DECLARE_FINAL_TYPE(FuUsbDeviceMsDs20,
		     fu_usb_device_ms_ds20,
		     FU,
		     USB_DEVICE_MS_DS20,
		     FuUsbDeviceDs20)

FuFirmware *
fu_usb_device_ms_ds20_new(void);
