/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-console.h"

gchar *
fu_util_bios_setting_to_string(FwupdBiosSetting *setting, guint idt) G_GNUC_NON_NULL(1);
gboolean
fu_util_bios_setting_matches_args(FwupdBiosSetting *setting, gchar **values) G_GNUC_NON_NULL(1, 2);
gboolean
fu_util_bios_setting_console_print(FuConsole *console,
				   gchar **values,
				   GPtrArray *settings,
				   GError **error) G_GNUC_NON_NULL(1, 2, 3);
GHashTable *
fu_util_bios_settings_parse_argv(gchar **input, GError **error) G_GNUC_NON_NULL(1);
