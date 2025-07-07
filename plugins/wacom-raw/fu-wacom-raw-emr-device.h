/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-wacom-raw-device.h"

#define FU_TYPE_WACOM_RAW_EMR_DEVICE (fu_wacom_raw_emr_device_get_type())
G_DECLARE_FINAL_TYPE(FuWacomRawEmrDevice,
		     fu_wacom_raw_emr_device,
		     FU,
		     WACOM_RAW_EMR_DEVICE,
		     FuWacomRawDevice)
