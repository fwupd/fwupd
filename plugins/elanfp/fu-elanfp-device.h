/*
 * Copyright (C) 2021 Michael Cheng <michael.cheng@emc.com.tw>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_ELANFP_DEVICE (fu_elanfp_device_get_type())
G_DECLARE_FINAL_TYPE(FuElanfpDevice, fu_elanfp_device, FU, ELANFP_DEVICE, FuUsbDevice)
