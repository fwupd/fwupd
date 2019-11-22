/*
 * Copyright (C) 2017-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-udev-device.h"

#define FU_TYPE_UNIFYING_PERIPHERAL (fu_logitech_hidpp_peripheral_get_type ())
G_DECLARE_FINAL_TYPE (FuLogitechHidPpPeripheral, fu_logitech_hidpp_peripheral, FU, UNIFYING_PERIPHERAL, FuUdevDevice)
