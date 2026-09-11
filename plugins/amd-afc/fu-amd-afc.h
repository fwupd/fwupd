/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

/* native AMD firmware configuration parser and BIOS setting provider */
#define FU_TYPE_AMD_AFC_BIOS_SETTING (fu_amd_afc_bios_setting_get_type())
G_DECLARE_FINAL_TYPE(FuAmdAfcBiosSetting,
		     fu_amd_afc_bios_setting,
		     FU,
		     AMD_AFC_BIOS_SETTING,
		     FuBiosSetting)

#define FU_TYPE_AMD_AFC_STATE (fu_amd_afc_state_get_type())
G_DECLARE_FINAL_TYPE(FuAmdAfcState, fu_amd_afc_state, FU, AMD_AFC_STATE, GObject)

FuAmdAfcState *
fu_amd_afc_state_new(FuContext *ctx);
gboolean
fu_amd_afc_state_parse_table(FuAmdAfcState *self, GBytes *bytes, GError **error);
guint
fu_amd_afc_state_get_setting_count(FuAmdAfcState *self);
gboolean
fu_amd_afc_state_add_bios_settings(FuAmdAfcState *self, GError **error);
