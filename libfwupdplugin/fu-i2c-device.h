/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-udev-device.h"

#define FU_TYPE_I2C_DEVICE (fu_i2c_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuI2cDevice, fu_i2c_device, FU, I2C_DEVICE, FuUdevDevice)

struct _FuI2cDeviceClass {
	FuUdevDeviceClass parent_class;
	gpointer __reserved[31];
};

guint
fu_i2c_device_get_bus_number(FuI2cDevice *self);
void
fu_i2c_device_set_bus_number(FuI2cDevice *self, guint bus_number);
gboolean
fu_i2c_device_read(FuI2cDevice *self, guint8 *buf, gsize bufsz, GError **error)
    G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_i2c_device_write(FuI2cDevice *self, const guint8 *buf, gsize bufsz, GError **error)
    G_GNUC_WARN_UNUSED_RESULT;
