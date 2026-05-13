/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-context.h"
#include "fu-hwids.h"
#include "fu-kenv.h"

FuHwids *
fu_hwids_new(void);
void
fu_hwids_add_chid(FuHwids *self, const gchar *key, const gchar *value);
gboolean
fu_hwids_setup(FuHwids *self, GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
gboolean
fu_hwids_config_setup(FuContext *ctx, FuHwids *self, GError **error) G_GNUC_NON_NULL(1);
gboolean
fu_hwids_dmi_setup(FuContext *ctx, FuHwids *self, GError **error) G_GNUC_NON_NULL(1);
gboolean
fu_hwids_fdt_setup(FuContext *ctx, FuHwids *self, GError **error) G_GNUC_NON_NULL(1);
gboolean
fu_hwids_kenv_setup(FuContext *ctx, FuHwids *self, GError **error) G_GNUC_NON_NULL(1);
gboolean
fu_hwids_darwin_setup(FuContext *ctx, FuHwids *self, GError **error) G_GNUC_NON_NULL(1);
gboolean
fu_hwids_smbios_setup(FuContext *ctx, FuHwids *self, GError **error) G_GNUC_NON_NULL(1);
