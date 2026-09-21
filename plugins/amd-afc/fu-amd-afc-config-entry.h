/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

typedef struct {
	GPtrArray *path; /* gchar * */
	gchar *value;
} FuAmdAfcConfigEntry;

void
fu_amd_afc_config_entry_free(FuAmdAfcConfigEntry *self);
