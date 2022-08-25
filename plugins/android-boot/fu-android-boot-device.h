/*
 * Copyright (C) 2022 Dylan Van Assche <me@dylanvanassche.be>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_ANDROID_BOOT_DEVICE (fu_android_boot_device_get_type())

G_DECLARE_FINAL_TYPE(FuAndroidBootDevice,
		     fu_android_boot_device,
		     FU,
		     ANDROID_BOOT_DEVICE,
		     FuUdevDevice)
