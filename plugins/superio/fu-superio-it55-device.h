/*
 * Copyright (C) 2021, TUXEDO Computers GmbH
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-superio-device.h"

#define FU_TYPE_EC_IT55_DEVICE (fu_superio_it55_device_get_type ())
G_DECLARE_FINAL_TYPE (FuEcIt55Device, fu_superio_it55_device, FU, SUPERIO_IT55_DEVICE, FuSuperioDevice)
