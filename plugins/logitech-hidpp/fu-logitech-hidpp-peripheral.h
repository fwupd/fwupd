/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-logitech-hidpp-device.h"

#define FU_TYPE_HIDPP_PERIPHERAL (fu_logitech_hidpp_peripheral_get_type ())
G_DECLARE_FINAL_TYPE (FuLogitechHidPpPeripheral, fu_logitech_hidpp_peripheral, FU, HIDPP_PERIPHERAL, FuLogitechHidPpDevice)
