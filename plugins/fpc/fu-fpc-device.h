/*
 * Copyright (C) 2022 Haowei Lo <haowei.lo@fingerprints.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_FPC_DEVICE (fu_fpc_device_get_type())
G_DECLARE_FINAL_TYPE(FuFpcDevice, fu_fpc_device, FU, FPC_DEVICE, FuUsbDevice)
