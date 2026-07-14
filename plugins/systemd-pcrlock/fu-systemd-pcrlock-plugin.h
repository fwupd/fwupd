/*
 * Copyright 2026 Luca Boccassi <luca.boccassi@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_SYSTEMD_PCRLOCK_PLUGIN (fu_systemd_pcrlock_plugin_get_type())

G_DECLARE_FINAL_TYPE(FuSystemdPcrlockPlugin,
		     fu_systemd_pcrlock_plugin,
		     FU,
		     SYSTEMD_PCRLOCK_PLUGIN,
		     FuPlugin)
