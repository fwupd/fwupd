/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#pragma once
#include <glib-object.h>
#include "fu-plugin.h"

#define PORT_NAME "Port"
#define PROGRAMMER_NAME "Programmer"
#define DEVICE_NAME "Device"
#define DEVICE_PROTOCOL "Protocol"
#define DEVICE_VENDOR_NAME "VendorName"

#define FU_TYPE_I2C_DEVICE (fu_i2c_device_get_type ())
/* Define as final type for now since not yet seeing devices
 * need to be handled separately. */
G_DECLARE_FINAL_TYPE (FuI2cDevice, fu_i2c_device, FU,
		      I2C_DEVICE, FuDevice)
