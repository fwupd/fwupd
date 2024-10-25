/*
 * Copyright 2024 Dell Technologies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#pragma once

#include <fwupdplugin.h>

/* Device IDs: USB RMM */
#define DELL_K2_USB_RMM_PID 0xB0A4

#define FU_TYPE_DELL_K2_RMM (fu_dell_k2_rmm_get_type())
G_DECLARE_FINAL_TYPE(FuDellK2Rmm, fu_dell_k2_rmm, FU, DELL_K2_RMM, FuHidDevice)

FuDellK2Rmm *
fu_dell_k2_rmm_new(FuUsbDevice *device);
void
fu_dell_k2_rmm_fix_version(FuDevice *device);
