/*
 * Copyright 1999-2023 Logitech, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_LOGITECH_TAP_HDMI_DEVICE (fu_logitech_tap_hdmi_device_get_type())
G_DECLARE_FINAL_TYPE(FuLogitechTapHdmiDevice,
		     fu_logitech_tap_hdmi_device,
		     FU,
		     LOGITECH_TAP_HDMI_DEVICE,
		     FuV4lDevice)

#define FU_LOGITECH_TAP_HDMI_DEVICE_FLAG_SENSOR_NEEDS_REBOOT "sensor-needs-reboot"
