/*
 * Copyright (C) 2022 Advanced Micro Devices Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_AMD_PMC_DEVICE (fu_amd_pmc_device_get_type())
G_DECLARE_FINAL_TYPE(FuAmdPmcDevice, fu_amd_pmc_device, FU, AMD_PMC_DEVICE, FuUdevDevice)
