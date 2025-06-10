/*
 * Copyright 2025 Mario Limonciello <superm1@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_LEGION_HID2_SIPO_DEVICE (fu_legion_hid2_sipo_device_get_type())
G_DECLARE_FINAL_TYPE(FuLegionHid2SipoDevice,
		     fu_legion_hid2_sipo_device,
		     FU,
		     LEGION_HID2_SIPO_DEVICE,
		     FuDevice)

FuDevice *
fu_legion_hid2_sipo_device_new(FuDevice *proxy) G_GNUC_NON_NULL(1);
