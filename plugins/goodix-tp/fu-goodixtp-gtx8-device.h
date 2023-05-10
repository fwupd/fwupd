/*
 * Copyright (C) 2023 Goodix.inc <xulinkun@goodix.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-goodixtp-hid-device.h"

#define FU_TYPE_GOODIXTP_GTX8_DEVICE (fu_goodixtp_gtx8_device_get_type())

G_DECLARE_FINAL_TYPE(FuGoodixtpGtx8Device,
		     fu_goodixtp_gtx8_device,
		     FU,
		     GOODIXTP_GTX8_DEVICE,
		     FuGoodixtpHidDevice)
