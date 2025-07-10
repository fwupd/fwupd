/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-udev-device.h"

#define FU_TYPE_I2C_DEVICE (fu_i2c_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuI2cDevice, fu_i2c_device, FU, I2C_DEVICE, FuUdevDevice)

struct _FuI2cDeviceClass {
	FuUdevDeviceClass parent_class;
};

/**
 * FU_I2C_DEVICE_PRIVATE_FLAG_NO_HWID_GUIDS:
 *
 * Do not add the HWID instance IDs.
 *
 * Since: 2.0.0
 */
extern GQuark FU_I2C_DEVICE_PRIVATE_FLAG_NO_HWID_GUIDS;

gboolean
fu_i2c_device_set_address(FuI2cDevice *self, guint8 address, gboolean force, GError **error)
    G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
gboolean
fu_i2c_device_read(FuI2cDevice *self, guint8 *buf, gsize bufsz, GError **error)
    G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 2);
gboolean
fu_i2c_device_write(FuI2cDevice *self, const guint8 *buf, gsize bufsz, GError **error)
    G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 2);
