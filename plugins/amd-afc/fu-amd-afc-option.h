/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

typedef struct {
	gchar *name;
	guint64 value;
} FuAmdAfcOption;

void
fu_amd_afc_option_export(FuAmdAfcOption *self, FuFirmwareExportFlags flags, XbBuilderNode *bn);
void
fu_amd_afc_option_free(FuAmdAfcOption *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuAmdAfcOption, fu_amd_afc_option_free)
