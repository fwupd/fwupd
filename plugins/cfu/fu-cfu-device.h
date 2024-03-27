/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_CFU_DEVICE (fu_cfu_device_get_type())
G_DECLARE_FINAL_TYPE(FuCfuDevice, fu_cfu_device, FU, CFU_DEVICE, FuHidDevice)
