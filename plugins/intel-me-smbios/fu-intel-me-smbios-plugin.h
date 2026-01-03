/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_INTEL_ME_SMBIOS_PLUGIN (fu_intel_me_smbios_plugin_get_type())
G_DECLARE_FINAL_TYPE(FuIntelMeSmbiosPlugin,
		     fu_intel_me_smbios_plugin,
		     FU,
		     INTEL_ME_SMBIOS_PLUGIN,
		     FuPlugin)
