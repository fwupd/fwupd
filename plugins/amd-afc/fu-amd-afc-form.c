/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-amd-afc-form.h"

void
fu_amd_afc_form_export(FuAmdAfcForm *self,
		       FuFirmwareExportFlags flags,
		       FwupdJsonObject *json_object)
{
	if (self->id != 0)
		fwupd_json_object_add_integer(json_object, "id", self->id);
	if (self->name != NULL)
		fwupd_json_object_add_string(json_object, "name", self->name);
	fwupd_json_object_add_boolean(json_object, "visited", self->visited);
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
