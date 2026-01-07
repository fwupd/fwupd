/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-security-attrs-struct.h"
#include "fu-security-attrs.h"

FuSecurityAttrs *
fu_security_attrs_new(void);
gchar *
fu_security_attrs_calculate_hsi(FuSecurityAttrs *self,
				const gchar *fwupd_version,
				FuSecurityAttrsFlags flags) G_GNUC_NON_NULL(1);
void
fu_security_attrs_depsolve(FuSecurityAttrs *self) G_GNUC_NON_NULL(1);
GVariant *
fu_security_attrs_to_variant(FuSecurityAttrs *self) G_GNUC_NON_NULL(1);
GPtrArray *
fu_security_attrs_get_all(FuSecurityAttrs *self, const gchar *fwupd_version) G_GNUC_NON_NULL(1);
void
fu_security_attrs_append_internal(FuSecurityAttrs *self, FwupdSecurityAttr *attr)
    G_GNUC_NON_NULL(1, 2);
gboolean
fu_security_attrs_is_valid(FuSecurityAttrs *self) G_GNUC_NON_NULL(1);
gboolean
fu_security_attrs_equal(FuSecurityAttrs *attrs1, FuSecurityAttrs *attrs2) G_GNUC_NON_NULL(1, 2);
GPtrArray *
fu_security_attrs_compare(FuSecurityAttrs *attrs1, FuSecurityAttrs *attrs2) G_GNUC_NON_NULL(1, 2);
