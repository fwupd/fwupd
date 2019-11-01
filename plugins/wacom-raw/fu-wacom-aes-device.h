/*
 * Copyright (C) 2018-2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-wacom-device.h"

#define FU_TYPE_WACOM_AES_DEVICE (fu_wacom_aes_device_get_type ())
G_DECLARE_FINAL_TYPE (FuWacomAesDevice, fu_wacom_aes_device, FU, WACOM_AES_DEVICE, FuWacomDevice)

FuWacomAesDevice	*fu_wacom_aes_device_new	(FuUdevDevice	*device);
