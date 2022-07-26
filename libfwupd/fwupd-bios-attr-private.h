/*
 * Copyright (C) 2022 Mario Limonciello <mario.limonciello@amd.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include <json-glib/json-glib.h>

#include "fwupd-bios-attr.h"

#pragma once

GVariant *
fwupd_bios_attr_to_variant(FwupdBiosAttr *self);
void
fwupd_bios_attr_to_json(FwupdBiosAttr *self, JsonBuilder *builder);
gboolean
fwupd_bios_attr_from_json(FwupdBiosAttr *self, JsonNode *json_node, GError **error);
