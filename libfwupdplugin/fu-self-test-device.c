/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-self-test-device.h"

struct _FuSelfTestDevice {
	FuDevice parent_instance;
};

G_DEFINE_TYPE(FuSelfTestDevice, fu_self_test_device, FU_TYPE_DEVICE)

static void
fu_self_test_device_init(FuSelfTestDevice *self)
{
}

static void
fu_self_test_device_class_init(FuSelfTestDeviceClass *klass)
{
}
