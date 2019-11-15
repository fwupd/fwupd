/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-plugin.h"

#define FU_TYPE_VLI_USBHUB_I2C_DEVICE (fu_vli_usbhub_i2c_device_get_type ())
G_DECLARE_FINAL_TYPE (FuVliUsbhubI2cDevice, fu_vli_usbhub_i2c_device, FU, VLI_USBHUB_I2C_DEVICE, FuDevice)

struct _FuVliUsbhubI2cDeviceClass
{
	FuDeviceClass		parent_class;
};

FuDevice	*fu_vli_usbhub_i2c_device_new	(FuVliUsbhubDevice	*parent);
