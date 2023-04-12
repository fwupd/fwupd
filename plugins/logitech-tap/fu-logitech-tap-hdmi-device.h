/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-logitech-tap-device.h"

#define FU_TYPE_LOGITECH_TAP_HDMI_DEVICE (fu_logitech_tap_hdmi_device_get_type())
G_DECLARE_FINAL_TYPE(FuLogitechTapHdmiDevice,
		     fu_logitech_tap_hdmi_device,
		     FU,
		     LOGITECH_TAP_HDMI_DEVICE,
		     FuLogitechTapDevice)
