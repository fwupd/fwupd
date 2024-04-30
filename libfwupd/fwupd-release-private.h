/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <json-glib/json-glib.h>

#include "fwupd-build.h"
#include "fwupd-release.h"

G_BEGIN_DECLS

void
fwupd_release_incorporate(FwupdRelease *self, FwupdRelease *donor) G_GNUC_NON_NULL(1, 2);

G_END_DECLS
