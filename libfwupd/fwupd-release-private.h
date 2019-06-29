/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>
#include <json-glib/json-glib.h>

#include "fwupd-release.h"

G_BEGIN_DECLS

FwupdRelease	*fwupd_release_from_variant		(GVariant	*value);
GPtrArray	*fwupd_release_array_from_variant	(GVariant	*value);
GVariant	*fwupd_release_to_variant		(FwupdRelease	*release);
void		 fwupd_release_to_json			(FwupdRelease *release,
							 JsonBuilder *builder);

G_END_DECLS

