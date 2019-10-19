/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-plugin.h"

#define FU_TYPE_VLI_USBHUB_DEVICE (fu_vli_usbhub_device_get_type ())
G_DECLARE_FINAL_TYPE (FuVliUsbhubDevice, fu_vli_usbhub_device, FU, VLI_USBHUB_DEVICE, FuUsbDevice)

struct _FuVliUsbhubDeviceClass
{
	FuUsbDeviceClass	parent_class;
};
