/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <json-glib/json-glib.h>

#include "fwupd-plugin.h"

G_BEGIN_DECLS

GVariant	*fwupd_plugin_to_variant		(FwupdPlugin	*self);
void		 fwupd_plugin_to_json			(FwupdPlugin	*self,
							 JsonBuilder	*builder);

G_END_DECLS
