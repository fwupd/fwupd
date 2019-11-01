/*
 * Copyright (C) 2018-2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-superio-device.h"

#define FU_TYPE_SUPERIO_IT85_DEVICE (fu_superio_it85_device_get_type ())
G_DECLARE_FINAL_TYPE (FuSuperioIt85Device, fu_superio_it85_device, FU, SUPERIO_IT85_DEVICE, FuSuperioDevice)
