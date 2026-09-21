/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-amd-afc-state.h"

G_BEGIN_DECLS

#define FU_TYPE_AMD_AFC_BIOS_SETTING (fu_amd_afc_bios_setting_get_type())
G_DECLARE_FINAL_TYPE(FuAmdAfcBiosSetting,
		     fu_amd_afc_bios_setting,
		     FU,
		     AMD_AFC_BIOS_SETTING,
		     FuBiosSetting)

FuAmdAfcBiosSetting *
fu_amd_afc_bios_setting_new(FuAmdAfcState *state, guint index);

G_END_DECLS
