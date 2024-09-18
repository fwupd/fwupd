/*
 * Copyright 2020 Benson Leung <bleung@chromium.org>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

typedef struct {
	gchar *boardname;
	gchar *triplet;
	gchar *sha1;
	gboolean dirty;
} FuCrosEcVersion;

FuCrosEcVersion *
fu_cros_ec_version_parse(const gchar *version_raw, GError **error);
void
fu_cros_ec_version_free(FuCrosEcVersion *version);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuCrosEcVersion, fu_cros_ec_version_free)
