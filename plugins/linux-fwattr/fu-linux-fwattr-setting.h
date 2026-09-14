/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_LINUX_FWATTR_SETTING (fu_linux_fwattr_setting_get_type())

G_DECLARE_FINAL_TYPE(FuLinuxFwattrSetting,
		     fu_linux_fwattr_setting,
		     FU,
		     LINUX_FWATTR_SETTING,
		     FuBiosSetting)

FuLinuxFwattrSetting *
fu_linux_fwattr_setting_new(FuContext *ctx,
			    const gchar *driver,
			    const gchar *path,
			    const gchar *name,
			    GError **error);
