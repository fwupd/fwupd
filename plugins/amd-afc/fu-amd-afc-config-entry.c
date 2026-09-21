/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-amd-afc-config-entry.h"

void
fu_amd_afc_config_entry_free(FuAmdAfcConfigEntry *self)
{
	g_ptr_array_unref(self->path);
	g_free(self->value);
	g_free(self);
}
