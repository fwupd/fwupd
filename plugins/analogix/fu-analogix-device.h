/*
 * Copyright (C) 2021 Xiaotian Cui <xtcui@analogixsemi.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */
#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_ANALOGIX_DEVICE (fu_analogix_device_get_type())
G_DECLARE_FINAL_TYPE(FuAnalogixDevice, fu_analogix_device, FU, ANALOGIX_DEVICE, FuUsbDevice)
