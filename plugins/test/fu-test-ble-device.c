/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-test-ble-device.h"

struct _FuTestBleDevice {
	FuBluezDevice		 parent_instance;
};

G_DEFINE_TYPE (FuTestBleDevice, fu_test_ble_device, FU_TYPE_BLUEZ_DEVICE)

static void
fu_test_ble_device_init (FuTestBleDevice *self)
{
	fu_device_set_protocol (FU_DEVICE (self), "org.test.testble");
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
}

static void
fu_test_ble_device_class_init (FuTestBleDeviceClass *klass)
{
}
