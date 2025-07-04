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

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_AMD_GPU_ATOM_FIRMWARE (fu_amd_gpu_atom_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuAmdGpuAtomFirmware,
		     fu_amd_gpu_atom_firmware,
		     FU,
		     AMD_GPU_ATOM_FIRMWARE,
		     FuOpromFirmware)

FuFirmware *
fu_amd_gpu_atom_firmware_new(void);

const gchar *
fu_amd_gpu_atom_firmware_get_vbios_pn(FuFirmware *firmware);
