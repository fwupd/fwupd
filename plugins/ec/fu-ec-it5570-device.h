/*
 * Copyright (C) 2021, TUXEDO Computers GmbH
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-ec-device.h"

#define FU_TYPE_EC_IT5570_DEVICE (fu_ec_it5570_device_get_type ())
G_DECLARE_FINAL_TYPE (FuEcIt5570Device, fu_ec_it5570_device, FU, EC_IT5570_DEVICE, FuEcDevice)
