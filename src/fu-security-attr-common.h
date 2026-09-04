/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

G_BEGIN_DECLS

gchar *
fu_security_attr_get_name_translated(FuSecurityAttr *attr) G_GNUC_NON_NULL(1);
const gchar *
fu_security_attr_get_title_translated(FuSecurityAttr *attr) G_GNUC_NON_NULL(1);
const gchar *
fu_security_attr_get_description_translated(FuSecurityAttr *attr) G_GNUC_NON_NULL(1);

G_END_DECLS
