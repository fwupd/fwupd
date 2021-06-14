/*
 * Copyright (C) 2020 Cypress Semiconductor Corporation.
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_CCGX_DMC_DEVICE (fu_ccgx_dmc_device_get_type ())
G_DECLARE_FINAL_TYPE (FuCcgxDmcDevice, fu_ccgx_dmc_device, FU, CCGX_DMC_DEVICE, FuUsbDevice)
