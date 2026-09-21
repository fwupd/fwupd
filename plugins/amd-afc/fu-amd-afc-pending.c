/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-amd-afc-pending.h"

void
fu_amd_afc_pending_export(FuAmdAfcPending *self, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	fu_xmlb_builder_insert_kx(bn, "setting", self->setting);
	if (self->value != NULL)
		fu_xmlb_builder_insert_kv(bn, "value", self->value);
}

void
fu_amd_afc_pending_free(FuAmdAfcPending *self)
{
	g_free(self->value);
	g_free(self);
}
