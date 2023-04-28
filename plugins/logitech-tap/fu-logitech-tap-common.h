/*
 * Copyright (c) 1999-2023 Logitech, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

/**
 * FU_LOGITECH_TAP_HDMI_DEVICE_FLAG_NEEDS_REBOOT:
 *
 * Firmware updated for HDMI component, trigger composite device reboot
 */
#define FU_LOGITECH_TAP_HDMI_DEVICE_FLAG_NEEDS_REBOOT (1ull << 1)

/**
 * FU_LOGITECH_TAP_DEVICE_FLAG_TYPE_HDMI:
 *
 * Used at Plugin level to determine if given FuDevice is FuLogitechTapHdmiDevice or not
 */
#define FU_LOGITECH_TAP_DEVICE_FLAG_TYPE_HDMI (1ull << 2)

/**
 * FU_LOGITECH_TAP_DEVICE_FLAG_TYPE_SENSOR:
 *
 * Used at Plugin level to determine if given FuDevice is FuLogitechTapSensorDevice or not
 */
#define FU_LOGITECH_TAP_DEVICE_FLAG_TYPE_SENSOR (1ull << 3)

gboolean
fu_logitech_tap_sensor_device_reboot_device(FuDevice *device, GError **error);
