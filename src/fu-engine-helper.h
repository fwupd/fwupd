/*
 * Copyright (C) 2020 Mario Limonciello <mario.limonciello@dell.com>
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */
#pragma once

#include "fu-engine.h"

gboolean
fu_engine_update_motd(FuEngine *self, GError **error);
gboolean
fu_engine_update_devices_file(FuEngine *self, GError **error);

GHashTable *
fu_engine_integrity_new(GError **error);
gchar *
fu_engine_integrity_to_string(GHashTable *self);
