/*
 * Copyright 2023 Framework Computer Inc
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_CCGX_PURE_HID_DEVICE (fu_ccgx_pure_hid_device_get_type())
G_DECLARE_FINAL_TYPE(FuCcgxPureHidDevice,
		     fu_ccgx_pure_hid_device,
		     FU,
		     CCGX_PURE_HID_DEVICE,
		     FuHidDevice)
