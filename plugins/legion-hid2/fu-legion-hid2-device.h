/*
 * Copyright 2024 Mario Limonciello <superm1@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_LEGION_HID2_DEVICE (fu_legion_hid2_device_get_type())
G_DECLARE_FINAL_TYPE(FuLegionHid2Device, fu_legion_hid2_device, FU, LEGION_HID2_DEVICE, FuHidDevice)
