/*
 * Copyright (c) 1999-2023 Logitech, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-logitech-tap-device.h"

#define FU_TYPE_LOGITECH_TAP_SENSOR_DEVICE (fu_logitech_tap_sensor_device_get_type())
G_DECLARE_FINAL_TYPE(FuLogitechTapSensorDevice,
		     fu_logitech_tap_sensor_device,
		     FU,
		     LOGITECH_TAP_SENSOR_DEVICE,
		     FuLogitechTapDevice)
