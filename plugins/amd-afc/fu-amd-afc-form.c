/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-amd-afc-form.h"

void
fu_amd_afc_form_export(FuAmdAfcForm *self, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	fu_xmlb_builder_insert_kx(bn, "id", self->id);
	if (self->name != NULL)
		fu_xmlb_builder_insert_kv(bn, "name", self->name);
	fu_xmlb_builder_insert_kb(bn, "visited", self->visited);
}

void
fu_amd_afc_form_free(FuAmdAfcForm *self)
{
	g_free(self->name);
	if (self->settings != NULL)
		g_array_unref(self->settings);
	if (self->refs != NULL)
		g_array_unref(self->refs);
	g_free(self);
}
