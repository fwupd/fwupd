/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

FwupdSecurityAttr *
fu_uefi_mok_attr_new(FuPlugin *plugin, const gchar *filename, GError **error);
