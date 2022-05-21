/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_VLI_USBHUB_RTD21XX_DEVICE (fu_vli_usbhub_rtd21xx_device_get_type())
G_DECLARE_FINAL_TYPE(FuVliUsbhubRtd21xxDevice,
		     fu_vli_usbhub_rtd21xx_device,
		     FU,
		     VLI_USBHUB_RTD21XX_DEVICE,
		     FuDevice)

FuDevice *
fu_vli_usbhub_rtd21xx_device_new(FuVliUsbhubDevice *parent);
