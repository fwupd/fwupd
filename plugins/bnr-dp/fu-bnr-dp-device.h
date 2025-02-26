/*
 * Copyright 2024 B&R Industrial Automation GmbH
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_BNR_DP_DEVICE (fu_bnr_dp_device_get_type())
G_DECLARE_FINAL_TYPE(FuBnrDpDevice, fu_bnr_dp_device, FU, BNR_DP_DEVICE, FuDpauxDevice)
