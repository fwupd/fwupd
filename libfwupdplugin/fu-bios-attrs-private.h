/*
 * Copyright (C) 2022 Mario Limonciello <mario.limonciello@amd.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <json-glib/json-glib.h>

#include "fu-bios-attrs.h"

gboolean
fu_bios_attrs_setup(FuBiosAttrs *self, GError **error);

GPtrArray *
fu_bios_attrs_get_all(FuBiosAttrs *self);

GVariant *
fu_bios_attrs_to_variant(FuBiosAttrs *self);
gboolean
fu_bios_attrs_from_json(FuBiosAttrs *self, JsonNode *json_node, GError **error);
