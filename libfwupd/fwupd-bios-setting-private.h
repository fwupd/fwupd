/*
 * Copyright (C) 2022 Mario Limonciello <mario.limonciello@amd.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <json-glib/json-glib.h>

#include "fwupd-bios-setting.h"

#pragma once

GVariant *
fwupd_bios_setting_to_variant(FwupdBiosSetting *self, gboolean trusted) G_GNUC_NON_NULL(1);
void
fwupd_bios_setting_to_json(FwupdBiosSetting *self, JsonBuilder *builder) G_GNUC_NON_NULL(1, 2);
gboolean
fwupd_bios_setting_from_json(FwupdBiosSetting *self, JsonNode *json_node, GError **error)
    G_GNUC_NON_NULL(1, 2);
