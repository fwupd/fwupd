/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-efivars.h"

gboolean
fu_efivars_set_secure_boot(FuEfivars *self, gboolean enabled, GError **error) G_GNUC_NON_NULL(1);
gboolean
fu_efivars_set_boot_current(FuEfivars *self, guint16 idx, GError **error) G_GNUC_NON_NULL(1);
gboolean
fu_efivars_build_boot_order(FuEfivars *self, GError **error, ...) G_GNUC_NON_NULL(1);
