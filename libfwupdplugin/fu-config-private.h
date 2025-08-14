/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-config.h"

typedef enum {
	FU_CONFIG_LOAD_FLAG_NONE = 0,
	FU_CONFIG_LOAD_FLAG_WATCH_FILES = 1 << 0,
	FU_CONFIG_LOAD_FLAG_FIX_PERMISSIONS = 1 << 1,
	FU_CONFIG_LOAD_FLAG_MIGRATE_FILES = 1 << 2,
} FuConfigLoadFlags;

FuConfig *
fu_config_new(void);
gboolean
fu_config_load(FuConfig *self, FuConfigLoadFlags flags, GError **error) G_GNUC_NON_NULL(1);
gboolean
fu_config_reset_defaults(FuConfig *self, const gchar *section, GError **error)
    G_GNUC_NON_NULL(1, 2);
