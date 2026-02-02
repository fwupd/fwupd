/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-config-struct.h"
#include "fu-config.h"
#include "fu-path-store.h"

FuConfig *
fu_config_new(FuPathStore *pstore);
gboolean
fu_config_load(FuConfig *self, FuConfigLoadFlags flags, GError **error) G_GNUC_NON_NULL(1);
void
fu_config_set_basename(FuConfig *self, const gchar *basename) G_GNUC_NON_NULL(1);
gboolean
fu_config_reset_defaults(FuConfig *self, const gchar *section, GError **error)
    G_GNUC_NON_NULL(1, 2);
void
fu_config_set_value_internal(FuConfig *self,
			     const gchar *section,
			     const gchar *key,
			     const gchar *value) G_GNUC_NON_NULL(1, 2, 3, 4);
