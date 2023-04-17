/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_SUPERIO_DEVICE (fu_superio_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuSuperioDevice, fu_superio_device, FU, SUPERIO_DEVICE, FuUdevDevice)

struct _FuSuperioDeviceClass {
	FuUdevDeviceClass parent_class;
};

gboolean
fu_superio_device_ec_read_data(FuSuperioDevice *self, guint8 *data, GError **error);
gboolean
fu_superio_device_ec_write_data(FuSuperioDevice *self, guint8 data, GError **error);
gboolean
fu_superio_device_ec_write_cmd(FuSuperioDevice *self, guint8 cmd, GError **error);
gboolean
fu_superio_device_reg_read(FuSuperioDevice *self, guint8 address, guint8 *data, GError **error);
gboolean
fu_superio_device_reg_write(FuSuperioDevice *self, guint8 address, guint8 data, GError **error);
gboolean
fu_superio_device_io_read(FuSuperioDevice *self, guint8 addr, guint8 *data, GError **error);
gboolean
fu_superio_device_io_write(FuSuperioDevice *self, guint8 addr, guint8 data, GError **error);
