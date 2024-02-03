/*
 * Copyright (C) 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-engine-struct.h"

#define FU_TYPE_CLIENT (fu_client_get_type())
G_DECLARE_FINAL_TYPE(FuClient, fu_client, FU, CLIENT, GObject)

FuClient *
fu_client_new(const gchar *sender);
const gchar *
fu_client_get_sender(FuClient *self);
const gchar *
fu_client_lookup_hint(FuClient *self, const gchar *key);
void
fu_client_insert_hint(FuClient *self, const gchar *key, const gchar *value);
void
fu_client_set_feature_flags(FuClient *self, FwupdFeatureFlags feature_flags);
FwupdFeatureFlags
fu_client_get_feature_flags(FuClient *self);
void
fu_client_remove_flag(FuClient *self, FuClientFlag flag);
gboolean
fu_client_has_flag(FuClient *self, FuClientFlag flag);
