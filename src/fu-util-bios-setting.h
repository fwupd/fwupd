/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#include "fwupd-bios-setting-private.h"

gchar *
fu_util_bios_setting_to_string(FwupdBiosSetting *setting, guint idt);
gboolean
fu_util_bios_setting_matches_args(FwupdBiosSetting *setting, gchar **values);
gboolean
fu_util_get_bios_setting_as_json(gchar **values, GPtrArray *settings, GError **error);
GHashTable *
fu_util_bios_settings_parse_argv(gchar **input, GError **error);
