/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-test-device.h"

struct _FuTestDevice {
	FuDevice parent_instance;
};

G_DEFINE_TYPE(FuTestDevice, fu_test_device, FU_TYPE_DEVICE)

static void
fu_test_device_init(FuTestDevice *self)
{
}

static void
fu_test_device_class_init(FuTestDeviceClass *klass)
{
}
