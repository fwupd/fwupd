/*
 *  Copyright (C) 2022 Jingle Wu <jingle.wu@emc.com.tw>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>
#include "fu-elantp-hid-device.h"

#define FU_TYPE_ELANTP_HID_HAPTIC_DEVICE (fu_elantp_hid_haptic_device_get_type())
G_DECLARE_FINAL_TYPE(FuElantpHidHapticDevice, fu_elantp_hid_haptic_device, FU, ELANTP_HID_HAPTIC_DEVICE, FuUdevDevice)

FuElantpHidHapticDevice *
fu_elantp_haptic_device_new(FuDevice *device);
