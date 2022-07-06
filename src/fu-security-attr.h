/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#include <json-glib/json-glib.h>

#include "fu-security-attrs-private.h"

gchar *
fu_security_attr_get_name(FwupdSecurityAttr *attr);
const gchar *
fu_security_attr_get_title(FwupdSecurityAttr *attr);
const gchar *
fu_security_attr_get_description(FwupdSecurityAttr *attr);
const gchar *
fu_security_attr_get_result(FwupdSecurityAttr *attr);
void
fu_security_attrs_to_json(FuSecurityAttrs *attrs, JsonBuilder *builder);
gchar *
fu_security_attrs_to_json_string(FuSecurityAttrs *attrs, GError **error);
gboolean
fu_security_attrs_from_json(FuSecurityAttrs *attrs, JsonNode *json_node, GError **error);
gboolean
fu_security_attrs_equal(FuSecurityAttrs *attrs1, FuSecurityAttrs *attrs2);
GPtrArray *
fu_security_attrs_compare(FuSecurityAttrs *attrs1, FuSecurityAttrs *attrs2);
const gchar *
fu_security_attr_result_to_string(FwupdSecurityAttrResult result);
