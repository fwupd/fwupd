/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "config.h"
#include "fu-i2c-device.h"

struct _FuI2cDevice
{
	FuDevice		 parent_instance;
};

G_DEFINE_TYPE (FuI2cDevice, fu_i2c_device, FU_TYPE_DEVICE)

static gboolean fu_i2c_device_write_firmware (FuDevice *device,
					      FuFirmware *firmware,
					      FwupdInstallFlags flags,
					      GError **error)
{
	g_set_error (error,
		     FWUPD_ERROR,
		     FWUPD_ERROR_INTERNAL,
		     "%s not implemented.",
		     __func__);
	return FALSE;
}

static void fu_i2c_device_init (FuI2cDevice *self)
{
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_INTERNAL);
}

static void fu_i2c_device_class_init (FuI2cDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	klass_device->write_firmware = fu_i2c_device_write_firmware;
}
