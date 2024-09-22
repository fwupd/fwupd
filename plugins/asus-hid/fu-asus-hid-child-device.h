/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 * Copyright 2024 Mario Limonciello <superm1@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-asus-hid-struct.h"

#define FU_TYPE_ASUS_HID_CHILD_DEVICE (fu_asus_hid_child_device_get_type())
G_DECLARE_FINAL_TYPE(FuAsusHidChildDevice,
		     fu_asus_hid_child_device,
		     FU,
		     ASUS_HID_CHILD_DEVICE,
		     FuHidDevice)

FuDevice *
fu_asus_hid_child_device_new(FuContext *ctx, guint8 idx) G_GNUC_NON_NULL(1);
