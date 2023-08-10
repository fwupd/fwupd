/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-context.h"
#include "fu-hwids.h"

FuHwids *
fu_hwids_new(void);
gboolean
fu_hwids_setup(FuHwids *self, GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_hwids_config_setup(FuContext *ctx, FuHwids *self, GError **error);
gboolean
fu_hwids_dmi_setup(FuContext *ctx, FuHwids *self, GError **error);
gboolean
fu_hwids_fdt_setup(FuContext *ctx, FuHwids *self, GError **error);
gboolean
fu_hwids_kenv_setup(FuContext *ctx, FuHwids *self, GError **error);
gboolean
fu_hwids_darwin_setup(FuContext *ctx, FuHwids *self, GError **error);
gboolean
fu_hwids_smbios_setup(FuContext *ctx, FuHwids *self, GError **error);
