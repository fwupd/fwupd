/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

/**
 * FuSecurityAttrsFlags:
 * @FU_SECURITY_ATTRS_FLAG_NONE:		No flags set
 * @FU_SECURITY_ATTRS_FLAG_ADD_VERSION:		Add the daemon version to the HSI string
 *
 * The flags to use when calculating an HSI version.
 **/
typedef enum {
	FU_SECURITY_ATTRS_FLAG_NONE = 0,
	FU_SECURITY_ATTRS_FLAG_ADD_VERSION = 1 << 0,
	/*< private >*/
	FU_SECURITY_ATTRS_FLAG_LAST
} FuSecurityAttrsFlags;

#include "fu-security-attrs.h"

FuSecurityAttrs *
fu_security_attrs_new(void);
gchar *
fu_security_attrs_calculate_hsi(FuSecurityAttrs *self, FuSecurityAttrsFlags flags);
void
fu_security_attrs_depsolve(FuSecurityAttrs *self);
GVariant *
fu_security_attrs_to_variant(FuSecurityAttrs *self);
GPtrArray *
fu_security_attrs_get_all(FuSecurityAttrs *self);
void
fu_security_attrs_append_internal(FuSecurityAttrs *self, FwupdSecurityAttr *attr);
gchar *
fu_security_attrs_to_json_string(FuSecurityAttrs *self, GError **error);
gboolean
fu_security_attrs_from_json(FuSecurityAttrs *self, JsonNode *json_node, GError **error);
gboolean
fu_security_attrs_equal(FuSecurityAttrs *attrs1, FuSecurityAttrs *attrs2);
GPtrArray *
fu_security_attrs_compare(FuSecurityAttrs *attrs1, FuSecurityAttrs *attrs2);
