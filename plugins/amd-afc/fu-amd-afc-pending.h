/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

typedef struct {
	guint setting;
	gchar *value;
} FuAmdAfcPending;

void
fu_amd_afc_pending_export(FuAmdAfcPending *self, FuFirmwareExportFlags flags, XbBuilderNode *bn);
void
fu_amd_afc_pending_free(FuAmdAfcPending *self);
