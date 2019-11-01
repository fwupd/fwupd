/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-plugin.h"

#define FU_TYPE_SOLOKEY_DEVICE (fu_solokey_device_get_type ())
G_DECLARE_FINAL_TYPE (FuSolokeyDevice, fu_solokey_device, FU, SOLOKEY_DEVICE, FuUsbDevice)
