/*
 * Copyright 2024 Logitech, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_LOGITECH_TAP_TOUCH_DEVICE (fu_logitech_tap_touch_device_get_type())
G_DECLARE_FINAL_TYPE(FuLogitechTapTouchDevice,
		     fu_logitech_tap_touch_device,
		     FU,
		     LOGITECH_TAP_TOUCH_DEVICE,
		     FuHidrawDevice)
