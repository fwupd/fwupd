/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupd.h>
#include <json-glib/json-glib.h>

#include "fu-security-attrs-private.h"

gchar *
fu_security_attr_get_name(FwupdSecurityAttr *attr);
const gchar *
fu_security_attr_get_result(FwupdSecurityAttr *attr);
void
fu_security_attrs_to_json(FuSecurityAttrs *attrs, JsonBuilder *builder);
gchar *
fu_security_attrs_to_json_string(FuSecurityAttrs *attrs, GError **error);
