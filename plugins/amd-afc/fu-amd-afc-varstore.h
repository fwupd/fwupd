/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

typedef struct {
	guint16 id;
	GBytes *data;
} FuAmdAfcVarstore;

void
fu_amd_afc_varstore_export(FuAmdAfcVarstore *self, FuFirmwareExportFlags flags, XbBuilderNode *bn);
void
fu_amd_afc_varstore_free(FuAmdAfcVarstore *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuAmdAfcVarstore, fu_amd_afc_varstore_free)
