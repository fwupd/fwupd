/*
 * Copyright (C) 2022 Google LLC
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-vbe-device.h"

#define FU_TYPE_VBE_SIMPLE_DEVICE (fu_vbe_simple_device_get_type())
G_DECLARE_FINAL_TYPE(FuVbeSimpleDevice, fu_vbe_simple_device, FU, VBE_SIMPLE_DEVICE, FuVbeDevice)
