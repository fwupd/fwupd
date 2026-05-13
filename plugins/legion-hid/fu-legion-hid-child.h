/*
 * Copyright 2025 lazro <2059899519@qq.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-legion-hid-struct.h"

#define FU_TYPE_LEGION_HID_CHILD (fu_legion_hid_child_get_type())
G_DECLARE_FINAL_TYPE(FuLegionHidChild, fu_legion_hid_child, FU, LEGION_HID_CHILD, FuDevice)

FuLegionHidChild *
fu_legion_hid_child_new(FuDevice *parent, FuLegionHidDeviceId id);
