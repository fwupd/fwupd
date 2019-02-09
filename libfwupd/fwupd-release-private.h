/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>

#include "fwupd-release.h"

G_BEGIN_DECLS

FwupdRelease	*fwupd_release_from_variant		(GVariant	*data);
GVariant	*fwupd_release_to_variant		(FwupdRelease	*release);

G_END_DECLS

