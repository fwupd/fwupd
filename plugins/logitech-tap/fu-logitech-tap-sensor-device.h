/*
 * Copyright 2023 Logitech, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_LOGITECH_TAP_SENSOR_DEVICE (fu_logitech_tap_sensor_device_get_type())
G_DECLARE_FINAL_TYPE(FuLogitechTapSensorDevice,
		     fu_logitech_tap_sensor_device,
		     FU,
		     LOGITECH_TAP_SENSOR_DEVICE,
		     FuHidrawDevice)
gboolean
fu_logitech_tap_sensor_device_reboot_device(FuLogitechTapSensorDevice *self, GError **error);
