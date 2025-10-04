/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-device.h"

#define FU_TYPE_SELF_TEST_DEVICE (fu_self_test_device_get_type())
G_DECLARE_FINAL_TYPE(FuSelfTestDevice, fu_self_test_device, FU, SELF_TEST_DEVICE, FuDevice)
