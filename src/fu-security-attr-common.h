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
