/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

G_BEGIN_DECLS

/* native AMD firmware configuration parser and BIOS setting provider */
#define FU_TYPE_AMD_AFC_BIOS_SETTING (fu_amd_afc_bios_setting_get_type())
G_DECLARE_FINAL_TYPE(FuAmdAfcBiosSetting,
		     fu_amd_afc_bios_setting,
		     FU,
		     AMD_AFC_BIOS_SETTING,
		     FwupdBiosSetting)

typedef struct _FuAmdAfcState FuAmdAfcState;

FuAmdAfcState *
fu_amd_afc_state_new(FuContext *ctx);
FuAmdAfcState *
fu_amd_afc_state_ref(FuAmdAfcState *self);
void
fu_amd_afc_state_unref(FuAmdAfcState *self);
gboolean
fu_amd_afc_state_parse_table(FuAmdAfcState *self, GBytes *bytes, GError **error);
guint
fu_amd_afc_state_get_setting_count(FuAmdAfcState *self);
gboolean
fu_amd_afc_state_add_bios_settings(FuAmdAfcState *self,
				   FuBiosSettings *bios_settings,
				   GError **error);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuAmdAfcState, fu_amd_afc_state_unref)

G_END_DECLS
