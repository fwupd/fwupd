/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <libfwupd/fwupd-security-attr.h>

#define FU_TYPE_SECURITY_ATTRS (fu_security_attrs_get_type())

G_DECLARE_FINAL_TYPE(FuSecurityAttrs, fu_security_attrs, FU, SECURITY_ATTRS, GObject)

void
fu_security_attrs_append(FuSecurityAttrs *self, FwupdSecurityAttr *attr) G_GNUC_NON_NULL(1, 2);
void
fu_security_attrs_remove_all(FuSecurityAttrs *self) G_GNUC_NON_NULL(1);
FwupdSecurityAttr *
fu_security_attrs_get_by_appstream_id(FuSecurityAttrs *self,
				      const gchar *appstream_id,
				      GError **error) G_GNUC_NON_NULL(1, 2);
GPtrArray *
fu_security_attrs_get_all_mutable(FuSecurityAttrs *self) G_GNUC_NON_NULL(1);
