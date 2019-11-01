/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-plugin.h"

#define FU_TYPE_RTS54HUB_DEVICE (fu_rts54hub_device_get_type ())
G_DECLARE_FINAL_TYPE (FuRts54HubDevice, fu_rts54hub_device, FU, RTS54HUB_DEVICE, FuUsbDevice)
