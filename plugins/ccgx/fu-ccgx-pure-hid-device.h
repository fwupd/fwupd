/*
 * Copyright (C) 2020 Cypress Semiconductor Corporation.
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_CCGX_PURE_HID_DEVICE (fu_ccgx_pure_hid_device_get_type())
G_DECLARE_FINAL_TYPE(FuCcgxPureHidDevice, fu_ccgx_pure_hid_device, FU, CCGX_PURE_HID_DEVICE, FuHidDevice)

typedef enum {
        INFO_E0    = 0xE0,
        COMMAND_E0 = 0xE1,
        WRITE_E2   = 0xE2,
        READ_E3    = 0xE3,
        Custom     = 0xE4,
} CcgHidReport;
