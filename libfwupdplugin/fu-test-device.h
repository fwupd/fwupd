/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-device.h"

#define FU_TYPE_TEST_DEVICE (fu_test_device_get_type())
G_DECLARE_FINAL_TYPE(FuTestDevice, fu_test_device, FU, TEST_DEVICE, FuDevice)
