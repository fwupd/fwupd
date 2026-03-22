/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_LXSTOUCH_DEVICE (fu_lxstouch_device_get_type())
G_DECLARE_FINAL_TYPE(FuLxstouchDevice, fu_lxstouch_device, FU, LXSTOUCH_DEVICE, FuHidrawDevice)
