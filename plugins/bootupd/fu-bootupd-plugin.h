/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_BOOTUPD_PLUGIN (fu_bootupd_plugin_get_type())

G_DECLARE_FINAL_TYPE(FuBootupdPlugin, fu_bootupd_plugin, FU, BOOTUPD_PLUGIN, FuPlugin)

void
fu_bootupd_plugin_set_varlink_address(FuBootupdPlugin *self, const gchar *varlink_address);
