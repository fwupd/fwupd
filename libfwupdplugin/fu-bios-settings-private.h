/*
 * Copyright 2022 Mario Limonciello <mario.limonciello@amd.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <json-glib/json-glib.h>

#include "fu-bios-settings.h"

gboolean
fu_bios_settings_setup(FuBiosSettings *self, GError **error) G_GNUC_NON_NULL(1);

GPtrArray *
fu_bios_settings_get_all(FuBiosSettings *self) G_GNUC_NON_NULL(1);

GHashTable *
fu_bios_settings_to_hash_kv(FuBiosSettings *self) G_GNUC_NON_NULL(1);
void
fu_bios_settings_add_attribute(FuBiosSettings *self, FwupdBiosSetting *attr) G_GNUC_NON_NULL(1, 2);
