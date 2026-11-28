/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-efivars.h"

G_BEGIN_DECLS

gboolean
fu_efivars_set_secure_boot(FuEfivars *self, gboolean enabled, GError **error) G_GNUC_NON_NULL(1);
gboolean
fu_efivars_set_boot_current(FuEfivars *self, guint16 idx, GError **error) G_GNUC_NON_NULL(1);
gboolean
fu_efivars_build_boot_order(FuEfivars *self, GError **error, ...) G_GNUC_NON_NULL(1);
FuPathStore *
fu_efivars_get_path_store(FuEfivars *self) G_GNUC_NON_NULL(1) G_GNUC_PURE;
const gchar *
fu_efivars_secure_boot_siglist_kind_to_guid(FuEfiSecureBootSiglistKind kind) G_GNUC_CONST;
const gchar *
fu_efivars_secure_boot_siglist_kind_to_name(FuEfiSecureBootSiglistKind kind) G_GNUC_CONST;
FuEfiSecureBootSiglistKind
fu_efivars_secure_boot_siglist_kind_from_guid_name(const gchar *guid, const gchar *name);

G_END_DECLS
