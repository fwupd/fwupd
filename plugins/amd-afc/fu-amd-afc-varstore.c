/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-amd-afc-varstore.h"

void
fu_amd_afc_varstore_export(FuAmdAfcVarstore *self, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	fu_xmlb_builder_insert_kx(bn, "id", self->id);
}

void
fu_amd_afc_varstore_free(FuAmdAfcVarstore *self)
{
	if (self->data != NULL)
		g_bytes_unref(self->data);
	g_free(self);
}
