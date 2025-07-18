/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-wacom-raw-device.h"

#define FU_TYPE_WACOM_RAW_AES_DEVICE (fu_wacom_raw_aes_device_get_type())
G_DECLARE_FINAL_TYPE(FuWacomRawAesDevice,
		     fu_wacom_raw_aes_device,
		     FU,
		     WACOM_RAW_AES_DEVICE,
		     FuWacomRawDevice)
