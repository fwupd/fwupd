/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-amd-afc-varstore.h"

void
fu_amd_afc_varstore_export(FuAmdAfcVarstore *self,
			   FuFirmwareExportFlags flags,
			   FwupdJsonObject *json_object)
{
	if (self->id != 0)
		fwupd_json_object_add_integer(json_object, "id", self->id);
}

void
fu_amd_afc_varstore_free(FuAmdAfcVarstore *self)
{
	if (self->data != NULL)
		g_bytes_unref(self->data);
	g_free(self);
}
