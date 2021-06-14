/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-wac-android-device.h"

struct _FuWacAndroidDevice
{
	FuHidDevice		 parent_instance;
};

G_DEFINE_TYPE (FuWacAndroidDevice, fu_wac_android_device, FU_TYPE_HID_DEVICE)

static void
fu_wac_android_device_init (FuWacAndroidDevice *self)
{
	fu_device_add_protocol (FU_DEVICE (self), "com.wacom.usb");
	fu_device_add_icon (FU_DEVICE (self), "input-tablet");
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_inhibit (FU_DEVICE (self), "hw",
			   "Switch into PC mode by holding down the "
			   "two outermost ExpressKeys for 4 seconds");
}

static void
fu_wac_android_device_class_init (FuWacAndroidDeviceClass *klass)
{
}
