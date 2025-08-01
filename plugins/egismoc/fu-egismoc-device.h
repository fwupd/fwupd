/*
 * Copyright 2025 Jason Huang <jason.huang@egistec.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_EGISMOC_DEVICE (fu_egismoc_device_get_type())
G_DECLARE_FINAL_TYPE(FuEgisMocDevice, fu_egismoc_device, FU, EGISMOC_DEVICE, FuUsbDevice)
