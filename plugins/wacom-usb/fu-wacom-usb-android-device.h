/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_WACOM_USB_ANDROID_DEVICE (fu_wacom_usb_android_device_get_type())
G_DECLARE_FINAL_TYPE(FuWacomUsbAndroidDevice,
		     fu_wacom_usb_android_device,
		     FU,
		     WACOM_USB_ANDROID_DEVICE,
		     FuHidDevice)
