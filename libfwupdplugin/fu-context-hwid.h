/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-context.h"

gboolean
fu_context_hwid_setup(FuContext *self, GError **error);
gboolean
fu_context_hwid_config_setup(FuContext *self, GError **error);
gboolean
fu_context_hwid_dmi_setup(FuContext *self, GError **error);
gboolean
fu_context_hwid_fdt_setup(FuContext *self, GError **error);
gboolean
fu_context_hwid_kenv_setup(FuContext *self, GError **error);
gboolean
fu_context_hwid_smbios_setup(FuContext *self, GError **error);
