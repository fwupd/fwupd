/*
 * Copyright (C) 2022 Mario Limonciello <mario.limonciello@amd.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <json-glib/json-glib.h>

#include "fu-bios-settings.h"

gboolean
fu_bios_settings_setup(FuBiosSettings *self, GError **error) G_GNUC_NON_NULL(1);

GPtrArray *
fu_bios_settings_get_all(FuBiosSettings *self) G_GNUC_NON_NULL(1);

GVariant *
fu_bios_settings_to_variant(FuBiosSettings *self, gboolean trusted) G_GNUC_NON_NULL(1);
gboolean
fu_bios_settings_from_json(FuBiosSettings *self, JsonNode *json_node, GError **error)
    G_GNUC_NON_NULL(1, 2);
gboolean
fu_bios_settings_from_json_file(FuBiosSettings *self, const gchar *fn, GError **error)
    G_GNUC_NON_NULL(1, 2);
GHashTable *
fu_bios_settings_to_hash_kv(FuBiosSettings *self) G_GNUC_NON_NULL(1);
