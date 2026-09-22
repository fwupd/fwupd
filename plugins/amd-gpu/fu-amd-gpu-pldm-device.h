/*
 * Copyright 2026 Advanced Micro Devices Inc.
 * All rights reserved.
 *
 * This file is provided under a dual MIT/LGPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 * AMD Chooses the MIT license part of Dual MIT/LGPLv2 license agreement.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_AMD_GPU_PLDM_DEVICE (fu_amd_gpu_pldm_device_get_type())
G_DECLARE_FINAL_TYPE(FuAmdGpuPldmDevice,
		     fu_amd_gpu_pldm_device,
		     FU,
		     AMD_GPU_PLDM_DEVICE,
		     FuUdevDevice)

FuDevice *
fu_amd_gpu_pldm_device_new(FuDevice *donor);
