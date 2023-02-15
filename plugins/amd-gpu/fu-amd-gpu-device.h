/*
 * Copyright (C) 2023 Advanced Micro Devices Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_AMDGPU_DEVICE (fu_amd_gpu_device_get_type())
G_DECLARE_FINAL_TYPE(FuAmdGpuDevice, fu_amd_gpu_device, FU, AMDGPU_DEVICE, FuUdevDevice)
