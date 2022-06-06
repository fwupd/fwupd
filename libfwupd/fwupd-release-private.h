/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <json-glib/json-glib.h>

#include "fwupd-release.h"

G_BEGIN_DECLS

GVariant *
fwupd_release_to_variant(FwupdRelease *self);
void
fwupd_release_to_json(FwupdRelease *self, JsonBuilder *builder);

G_END_DECLS
