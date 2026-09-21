/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-amd-afc-setting.h"

#define FU_TYPE_AMD_AFC_STATE (fu_amd_afc_state_get_type())
G_DECLARE_FINAL_TYPE(FuAmdAfcState, fu_amd_afc_state, FU, AMD_AFC_STATE, FuFirmware)

FuAmdAfcState *
fu_amd_afc_state_new(FuContext *ctx);
FuContext *
fu_amd_afc_state_get_context(FuAmdAfcState *self);

FuAmdAfcSetting *
fu_amd_afc_state_get_setting(FuAmdAfcState *self, guint setting_idx, GError **error);

gboolean
fu_amd_afc_state_parse_table(FuAmdAfcState *self, GBytes *bytes, GError **error);
guint
fu_amd_afc_state_get_setting_count(FuAmdAfcState *self);
gboolean
fu_amd_afc_state_add_bios_settings(FuAmdAfcState *self, GError **error);
gboolean
fu_amd_afc_state_store(FuAmdAfcState *self, guint index, const gchar *value, GError **error);
gboolean
fu_amd_afc_state_strings_parse_package(FuAmdAfcState *self,
				       GBytes *blob,
				       gsize offset,
				       GError **error);
