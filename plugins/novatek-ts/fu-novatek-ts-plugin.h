/*
 * Copyright 2026 Novatekmsp <novatekmsp@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_NOVATEK_TS_PLUGIN (fu_novatek_ts_plugin_get_type())
G_DECLARE_FINAL_TYPE(FuNovatekTsPlugin, fu_novatek_ts_plugin, FU, NOVATEK_TS_PLUGIN, FuPlugin)
