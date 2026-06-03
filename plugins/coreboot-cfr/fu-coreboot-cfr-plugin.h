/*
 * Copyright 2026 Star Labs
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_COREBOOT_CFR_PLUGIN (fu_coreboot_cfr_plugin_get_type())
G_DECLARE_FINAL_TYPE(FuCorebootCfrPlugin, fu_coreboot_cfr_plugin, FU, COREBOOT_CFR_PLUGIN, FuPlugin)
