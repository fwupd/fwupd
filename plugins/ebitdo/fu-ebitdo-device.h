/*
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-plugin.h"

#define FU_TYPE_EBITDO_DEVICE (fu_ebitdo_device_get_type ())
G_DECLARE_FINAL_TYPE (FuEbitdoDevice, fu_ebitdo_device, FU, EBITDO_DEVICE, FuUsbDevice)
