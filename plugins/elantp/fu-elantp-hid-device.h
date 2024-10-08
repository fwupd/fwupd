/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_ELANTP_HID_DEVICE (fu_elantp_hid_device_get_type())
G_DECLARE_FINAL_TYPE(FuElantpHidDevice, fu_elantp_hid_device, FU, ELANTP_HID_DEVICE, FuHidrawDevice)
