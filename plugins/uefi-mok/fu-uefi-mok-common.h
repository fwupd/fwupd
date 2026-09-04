/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

FuSecurityAttr *
fu_uefi_mok_attr_fw_new(FuPlugin *plugin, GBytes *blob);
FuSecurityAttr *
fu_uefi_mok_attr_nx_new(FuPlugin *plugin, GBytes *blob);
