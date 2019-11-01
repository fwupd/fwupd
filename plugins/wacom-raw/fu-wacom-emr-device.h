/*
 * Copyright (C) 2018-2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-wacom-device.h"

#define FU_TYPE_WACOM_EMR_DEVICE (fu_wacom_emr_device_get_type ())
G_DECLARE_FINAL_TYPE (FuWacomEmrDevice, fu_wacom_emr_device, FU, WACOM_EMR_DEVICE, FuWacomDevice)

FuWacomEmrDevice	*fu_wacom_emr_device_new	(FuUdevDevice	*device);
