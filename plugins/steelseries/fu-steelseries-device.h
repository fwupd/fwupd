/*
 * Copyright (C) 2016-2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-plugin.h"

G_BEGIN_DECLS

#define FU_TYPE_STEELSERIES_DEVICE (fu_steelseries_device_get_type ())
G_DECLARE_DERIVABLE_TYPE (FuSteelseriesDevice, fu_steelseries_device, FU, STEELSERIES_DEVICE, FuUsbDevice)

struct _FuSteelseriesDeviceClass
{
	FuUsbDeviceClass	parent_class;
};

G_END_DECLS
