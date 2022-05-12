/*
 * Copyright (C) 2021 Andrii Dushko <andrii.dushko@developex.net>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-corsair-bp.h"
#include "fu-corsair-common.h"

#define FU_TYPE_CORSAIR_DEVICE (fu_corsair_device_get_type())
G_DECLARE_FINAL_TYPE(FuCorsairDevice, fu_corsair_device, FU, CORSAIR_DEVICE, FuUsbDevice)

struct _FuCorsairDeviceClass {
	FuUsbDeviceClass parent_class;
};

FuCorsairDevice *
fu_corsair_device_new(FuCorsairDevice *parent, FuCorsairBp *bp);
