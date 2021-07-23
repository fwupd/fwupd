/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_WAC_ANDROID_DEVICE (fu_wac_android_device_get_type ())
G_DECLARE_FINAL_TYPE (FuWacAndroidDevice, fu_wac_android_device, FU, WAC_ANDROID_DEVICE, FuHidDevice)
