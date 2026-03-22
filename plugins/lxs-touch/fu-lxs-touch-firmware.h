/*
 * Copyright 2026 JS Park <mameforever2@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_LXS_TOUCH_FIRMWARE (fu_lxs_touch_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuLxsTouchFirmware, fu_lxs_touch_firmware, FU, LXS_TOUCH_FIRMWARE, FuFirmware)

FuFirmware *
fu_lxs_touch_firmware_new(void);
guint32
fu_lxs_touch_firmware_get_offset(FuLxsTouchFirmware *self);
