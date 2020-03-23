/*
 * Copyright (C) 2020 Cypress Semiconductor Corporation.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */
#pragma once

#include "config.h"
#include <gusb.h>
#include "fu-device.h"

#define FU_TYPE_CCGX_DOCK_BB (fu_ccgx_dock_bb_get_type ())
G_DECLARE_FINAL_TYPE (FuCcgxDockBb, fu_ccgx_dock_bb, FU, CCGX_DOCK_BB, FuUsbDevice)

gboolean	fu_ccgx_dock_bb_reboot (FuDevice *device, GError **error);
