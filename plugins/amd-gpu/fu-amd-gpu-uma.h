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

#include <fwupd.h>

#define FU_TYPE_AMD_GPU_UMA_SETTING (fu_amd_gpu_uma_setting_get_type())
G_DECLARE_FINAL_TYPE(FuAmdGpuUmaSetting,
		     fu_amd_gpu_uma_setting,
		     FU,
		     AMD_GPU_UMA_SETTING,
		     FwupdBiosSetting)

gboolean
fu_amd_gpu_uma_check_support(const gchar *device_sysfs_path, GError **error);

FwupdBiosSetting *
fu_amd_gpu_uma_get_setting(const gchar *device_sysfs_path, GError **error);
