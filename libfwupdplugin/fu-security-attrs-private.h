/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-security-attrs.h"

FuSecurityAttrs	*fu_security_attrs_new			(void);
gchar		*fu_security_attrs_calculate_hsi	(FuSecurityAttrs	*self);
void		 fu_security_attrs_depsolve		(FuSecurityAttrs	*self);
GVariant	*fu_security_attrs_to_variant		(FuSecurityAttrs	*self);
GPtrArray	*fu_security_attrs_get_all		(FuSecurityAttrs	*self);
