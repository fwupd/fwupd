/*
 * Copyright (C) 2023 Goodix.inc <xulinkun@goodix.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-goodixtp-hid-device.h"

#define FU_TYPE_GOODIXTP_BRLB_DEVICE (fu_goodixtp_brlb_device_get_type())

G_DECLARE_FINAL_TYPE(FuGoodixtpBrlbDevice,
		     fu_goodixtp_brlb_device,
		     FU,
		     GOODIXTP_BRLB_DEVICE,
		     FuGoodixtpHidDevice)
