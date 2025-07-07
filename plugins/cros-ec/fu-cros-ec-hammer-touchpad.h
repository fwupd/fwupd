/*
 * Copyright 2025 Hamed Elgizery
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-cros-ec-usb-device.h"

#define FU_TYPE_CROS_EC_HAMMER_TOUCHPAD (fu_cros_ec_hammer_touchpad_get_type())
G_DECLARE_FINAL_TYPE(FuCrosEcHammerTouchpad,
		     fu_cros_ec_hammer_touchpad,
		     FU,
		     CROS_EC_HAMMER_TOUCHPAD,
		     FuDevice)

FuCrosEcHammerTouchpad *
fu_cros_ec_hammer_touchpad_new(FuDevice *proxy);
