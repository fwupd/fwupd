/*
 * Copyright (C) 2022 Mario Limonciello <mario.limonciello@amd.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <json-glib/json-glib.h>

#include "fu-bios-settings.h"

gboolean
fu_bios_settings_setup(FuBiosSettings *self, GError **error);

GPtrArray *
fu_bios_settings_get_all(FuBiosSettings *self);

GVariant *
fu_bios_settings_to_variant(FuBiosSettings *self, gboolean trusted);
gboolean
fu_bios_settings_from_json(FuBiosSettings *self, JsonNode *json_node, GError **error);
gboolean
fu_bios_settings_from_json_file(FuBiosSettings *self, const gchar *fn, GError **error);
GHashTable *
fu_bios_settings_to_hash_kv(FuBiosSettings *self);
