/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-plugin.h"

#include "fu-vli-pd-common.h"

#define FU_TYPE_VLI_USBHUB_PD_DEVICE (fu_vli_usbhub_pd_device_get_type ())
G_DECLARE_FINAL_TYPE (FuVliUsbhubPdDevice, fu_vli_usbhub_pd_device, FU, VLI_USBHUB_PD_DEVICE, FuDevice)

struct _FuVliUsbhubPdDeviceClass
{
	FuDeviceClass		parent_class;
};

FuDevice	*fu_vli_usbhub_pd_device_new	(FuVliPdHdr	*hdr);
