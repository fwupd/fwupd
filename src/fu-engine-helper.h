/*
 * Copyright 2020 Mario Limonciello <mario.limonciello@dell.com>
 * Copyright 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#pragma once

#include "fu-engine.h"

gboolean
fu_engine_update_motd(FuEngine *self, GError **error) G_GNUC_NON_NULL(1);
gboolean
fu_engine_update_devices_file(FuEngine *self, GError **error) G_GNUC_NON_NULL(1);

GHashTable *
fu_engine_integrity_new(FuContext *ctx, GError **error);
gchar *
fu_engine_integrity_to_string(GHashTable *self);

GError *
fu_engine_error_array_get_best(GPtrArray *errors);
gchar *
fu_engine_build_machine_id(const gchar *salt, GError **error);
