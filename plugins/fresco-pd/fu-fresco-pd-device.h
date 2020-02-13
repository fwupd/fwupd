/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-plugin.h"

#define FU_TYPE_FRESCO_PD_DEVICE (fu_fresco_pd_device_get_type ())
G_DECLARE_FINAL_TYPE (FuFrescoPdDevice, fu_fresco_pd_device, FU, FRESCO_PD_DEVICE, FuUsbDevice)

struct _FuFrescoPdDeviceClass
{
	FuUsbDeviceClass	parent_class;
};
