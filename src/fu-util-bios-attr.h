/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#include "fwupd-bios-attr-private.h"

gchar *
fu_util_bios_attr_to_string(FwupdBiosAttr *attr, guint idt);
gboolean
fu_util_bios_attr_matches_args(FwupdBiosAttr *attr, gchar **values);
gboolean
fu_util_get_bios_attr_as_json(gchar **values, GPtrArray *attrs, GError **error);
