/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-amd-afc-option.h"

void
fu_amd_afc_option_export(FuAmdAfcOption *self, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	if (self->name != NULL)
		fu_xmlb_builder_insert_kv(bn, "name", self->name);
	fu_xmlb_builder_insert_kx(bn, "value", self->value);
}

void
fu_amd_afc_option_free(FuAmdAfcOption *self)
{
	g_free(self->name);
	g_free(self);
}
