/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_ELANTP_I2C_DEVICE_ABSOLUTE (1 << 0)

#define FU_TYPE_ELANTP_I2C_DEVICE (fu_elantp_i2c_device_get_type())
G_DECLARE_FINAL_TYPE(FuElantpI2cDevice, fu_elantp_i2c_device, FU, ELANTP_I2C_DEVICE, FuUdevDevice)
