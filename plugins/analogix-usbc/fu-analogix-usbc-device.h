/*
 * Copyright (C) 2021 Xiaotian Cui <xtcui@analogixsemi.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */
#pragma once

#include "fu-plugin.h"

#define FU_TYPE_ANALOGIX_USBC_DEVICE (fu_analogix_usbc_device_get_type ())
G_DECLARE_FINAL_TYPE (FuAnalogixUsbcDevice, fu_analogix_usbc_device, FU,\
		      ANALOGIX_USBC_DEVICE, FuUsbDevice)

struct _FuAnalogixUsbcDeviceClass
{
	FuUsbDeviceClass parent_class;
};
