/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include <json-glib/json-glib.h>

gchar *
fu_security_attr_get_name(FwupdSecurityAttr *attr) G_GNUC_NON_NULL(1);
const gchar *
fu_security_attr_get_title(FwupdSecurityAttr *attr) G_GNUC_NON_NULL(1);
const gchar *
fu_security_attr_get_description(FwupdSecurityAttr *attr) G_GNUC_NON_NULL(1);
