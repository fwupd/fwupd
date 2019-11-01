/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-plugin.h"

#define FU_TYPE_NITROKEY_DEVICE (fu_nitrokey_device_get_type ())
G_DECLARE_DERIVABLE_TYPE (FuNitrokeyDevice, fu_nitrokey_device, FU, NITROKEY_DEVICE, FuUsbDevice)

struct _FuNitrokeyDeviceClass
{
	FuUsbDeviceClass	parent_class;
};
