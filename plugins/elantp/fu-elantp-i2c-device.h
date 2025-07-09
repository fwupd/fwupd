/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

extern GQuark FU_ELANTP_I2C_DEVICE_ABSOLUTE;

#define FU_TYPE_ELANTP_I2C_DEVICE (fu_elantp_i2c_device_get_type())
G_DECLARE_FINAL_TYPE(FuElantpI2cDevice, fu_elantp_i2c_device, FU, ELANTP_I2C_DEVICE, FuI2cDevice)
