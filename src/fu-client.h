/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-engine-struct.h"

#define FU_TYPE_CLIENT (fu_client_get_type())
G_DECLARE_FINAL_TYPE(FuClient, fu_client, FU, CLIENT, GObject)

FuClient *
fu_client_new(const gchar *sender);
const gchar *
fu_client_get_sender(FuClient *self) G_GNUC_NON_NULL(1);
const gchar *
fu_client_lookup_hint(FuClient *self, const gchar *key) G_GNUC_NON_NULL(1, 2);
void
fu_client_insert_hint(FuClient *self, const gchar *key, const gchar *value)
    G_GNUC_NON_NULL(1, 2, 3);
void
fu_client_set_feature_flags(FuClient *self, FwupdFeatureFlags feature_flags) G_GNUC_NON_NULL(1);
FwupdFeatureFlags
fu_client_get_feature_flags(FuClient *self) G_GNUC_NON_NULL(1);
void
fu_client_remove_flag(FuClient *self, FuClientFlag flag) G_GNUC_NON_NULL(1);
gboolean
fu_client_has_flag(FuClient *self, FuClientFlag flag) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
