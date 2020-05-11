/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>
#include <json-glib/json-glib.h>

#include "fwupd-security-attr.h"

G_BEGIN_DECLS

GVariant	*fwupd_security_attr_to_variant		(FwupdSecurityAttr	*self);
void		 fwupd_security_attr_to_json		(FwupdSecurityAttr	*self,
							 JsonBuilder		*builder);

G_END_DECLS

