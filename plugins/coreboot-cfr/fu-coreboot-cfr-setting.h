/*
 * Copyright 2026 Sean Rhodes <sean@starlabs.systems>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-coreboot-cfr-struct.h"

#define FU_TYPE_COREBOOT_CFR_SETTING (fu_coreboot_cfr_setting_get_type())
G_DECLARE_FINAL_TYPE(FuCorebootCfrSetting,
		     fu_coreboot_cfr_setting,
		     FU,
		     COREBOOT_CFR_SETTING,
		     FwupdBiosSetting)

FuCorebootCfrSetting *
fu_coreboot_cfr_setting_new(FuContext *ctx, FuCorebootCfrApplyMethod runtime_apply_method)
    G_GNUC_NON_NULL(1);
void
fu_coreboot_cfr_setting_set_id(FuCorebootCfrSetting *self, const gchar *name);
void
fu_coreboot_cfr_setting_set_runtime_apply_id(FuCorebootCfrSetting *self, guint32 runtime_apply_id);
