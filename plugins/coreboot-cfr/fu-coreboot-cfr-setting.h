/*
 * Copyright 2026 Star Labs
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_COREBOOT_CFR_SETTING (fu_coreboot_cfr_setting_get_type())
G_DECLARE_FINAL_TYPE(FuCorebootCfrSetting,
		     fu_coreboot_cfr_setting,
		     FU,
		     COREBOOT_CFR_SETTING,
		     FwupdBiosSetting)

FwupdBiosSetting *
fu_coreboot_cfr_setting_new(FuEfivars *efivars,
			    const gchar *name,
			    const gchar *ui_name,
			    const gchar *description,
			    FwupdBiosSettingKind kind,
			    FuEfiVariableAttrs attrs,
			    guint32 runtime_apply_method,
			    guint32 runtime_apply_id,
			    guint32 lower_bound,
			    guint32 upper_bound,
			    guint32 scalar_increment,
			    GPtrArray *possible_values,
			    GHashTable *value_map,
			    GHashTable *reverse_value_map,
			    const gchar *pending_reboot_path,
			    GError **error) G_GNUC_NON_NULL(1, 2, 3);
