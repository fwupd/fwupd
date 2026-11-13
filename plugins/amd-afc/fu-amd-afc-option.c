/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-amd-afc-option.h"

void
fu_amd_afc_option_export(FuAmdAfcOption *self,
			 FuFirmwareExportFlags flags,
			 FwupdJsonObject *json_object)
{
	if (self->name != NULL)
		fwupd_json_object_add_string(json_object, "name", self->name);
	if (self->value != 0)
		fwupd_json_object_add_integer(json_object, "value", (gint64)self->value);
}

void
fu_amd_afc_option_free(FuAmdAfcOption *self)
{
	g_free(self->name);
	g_free(self);
}
