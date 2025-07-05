/*
 * Copyright 2024 Advanced Micro Devices Inc.
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

#define FU_TYPE_AMD_KRIA_PERSISTENT_FIRMWARE (fu_amd_kria_persistent_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuAmdKriaPersistentFirmware,
		     fu_amd_kria_persistent_firmware,
		     FU,
		     AMD_KRIA_PERSISTENT_FIRMWARE,
		     FuFirmware)

FuFirmware *
fu_amd_kria_persistent_firmware_new(void);

gboolean
fu_amd_kria_persistent_firmware_booted_image_a(FuAmdKriaPersistentFirmware *self);
