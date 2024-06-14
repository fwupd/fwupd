/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-efivars.h"
#include "fu-volume.h"

gboolean
fu_efivars_set_secure_boot(FuEfivars *self, gboolean enabled, GError **error) G_GNUC_NON_NULL(1);
gboolean
fu_efivars_set_boot_current(FuEfivars *self, guint16 idx, GError **error) G_GNUC_NON_NULL(1);
gboolean
fu_efivars_build_boot_order(FuEfivars *self, GError **error, ...) G_GNUC_NON_NULL(1);
gboolean
fu_efivars_create_boot_entry_for_volume(FuEfivars *self,
					guint16 idx,
					FuVolume *volume,
					const gchar *name,
					const gchar *target,
					GError **error) G_GNUC_NON_NULL(1, 3, 4, 5);
