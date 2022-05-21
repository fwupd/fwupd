/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_VLI_USBHUB_PD_DEVICE (fu_vli_usbhub_pd_device_get_type())
G_DECLARE_FINAL_TYPE(FuVliUsbhubPdDevice,
		     fu_vli_usbhub_pd_device,
		     FU,
		     VLI_USBHUB_PD_DEVICE,
		     FuDevice)

FuDevice *
fu_vli_usbhub_pd_device_new(FuVliUsbhubDevice *parent);
