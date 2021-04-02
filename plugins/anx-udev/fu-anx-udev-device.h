/*
 * Copyright (C) 2021 Xiaotian Cui <xtcui@analogixsemi.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */
#pragma once

#include "fu-plugin.h"

#define FU_TYPE_ANX_UDEV_DEVICE (fu_anx_udev_device_get_type ())
G_DECLARE_FINAL_TYPE (FuAnxUdevDevice, fu_anx_udev_device, FU,\
                            ANX_UDEV_DEVICE, FuUsbDevice)

struct _FuAnxUdevDeviceClass
{
    FuUsbDeviceClass   parent_class;
};