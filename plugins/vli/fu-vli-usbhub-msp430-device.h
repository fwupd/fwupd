/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-plugin.h"

#define FU_TYPE_VLI_USBHUB_MSP430_DEVICE (fu_vli_usbhub_msp430_device_get_type ())
G_DECLARE_FINAL_TYPE (FuVliUsbhubMsp430Device, fu_vli_usbhub_msp430_device, FU, VLI_USBHUB_MSP430_DEVICE, FuDevice)

struct _FuVliUsbhubMsp430DeviceClass
{
	FuDeviceClass		parent_class;
};

FuDevice	*fu_vli_usbhub_msp430_device_new	(FuVliUsbhubDevice	*parent);
