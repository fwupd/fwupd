/*
 * Copyright (C) 2020 Cypress Semiconductor Corporation.
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_CCGX_HID_DEVICE (fu_ccgx_hid_device_get_type ())
G_DECLARE_FINAL_TYPE (FuCcgxHidDevice, fu_ccgx_hid_device, FU, CCGX_HID_DEVICE, FuHidDevice)
