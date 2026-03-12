/*
 * Copyright 2026 LXS <support@lxsemicon.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_LXS_TOUCH_DEVICE (fu_lxs_touch_device_get_type())
G_DECLARE_FINAL_TYPE(FuLxsTouchDevice, fu_lxs_touch_device, FU, LXS_TOUCH_DEVICE, FuUdevDevice)
