/*
 * Copyright 2021 Peter Marheine <pmarheine@chromium.org>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_REALTEK_MST_DEVICE (fu_realtek_mst_device_get_type())
G_DECLARE_FINAL_TYPE(FuRealtekMstDevice, fu_realtek_mst_device, FU, REALTEK_MST_DEVICE, FuI2cDevice)
