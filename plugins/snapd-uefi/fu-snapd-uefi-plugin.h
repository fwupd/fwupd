/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_SNAPD_UEFI_PLUGIN (fu_snapd_uefi_plugin_get_type())

G_DECLARE_FINAL_TYPE(FuSnapPlugin, fu_snapd_uefi_plugin, FU, SNAPD_UEFI_PLUGIN, FuPlugin)
