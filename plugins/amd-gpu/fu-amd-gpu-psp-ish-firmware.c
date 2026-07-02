/*
 * Copyright 2023 Advanced Micro Devices Inc.
 * All rights reserved.
 *
 * This file is provided under a dual MIT/LGPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 * AMD Chooses the MIT license part of Dual MIT/LGPLv2 license agreement.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#include "config.h"

#include "fu-amd-gpu-psp-ish-firmware.h"
#include "fu-amd-gpu-psp-l2-firmware.h"

struct _FuAmdGpuPspIshFirmware {
	FuFirmware parent_instance;
};

G_DEFINE_TYPE(FuAmdGpuPspIshFirmware, fu_amd_gpu_psp_ish_firmware, FU_TYPE_FIRMWARE)

static void
fu_amd_gpu_psp_ish_firmware_init(FuAmdGpuPspIshFirmware *self)
{
}

static void
fu_amd_gpu_psp_ish_firmware_class_init(FuAmdGpuPspIshFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	fu_firmware_add_image_gtype(firmware_class, FU_TYPE_AMD_GPU_PSP_L2_FIRMWARE);
}

FuFirmware *
fu_amd_gpu_psp_ish_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_AMD_GPU_PSP_ISH_FIRMWARE, NULL));
}
