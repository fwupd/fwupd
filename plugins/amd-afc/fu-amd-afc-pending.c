/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-amd-afc-pending.h"

void
fu_amd_afc_pending_export(FuAmdAfcPending *self,
			  FuFirmwareExportFlags flags,
			  FwupdJsonObject *json_object)
{
	if (self->setting != 0)
		fwupd_json_object_add_integer(json_object, "setting", self->setting);
	if (self->value != NULL)
		fwupd_json_object_add_string(json_object, "value", self->value);
}

void
fu_amd_afc_pending_free(FuAmdAfcPending *self)
{
	g_free(self->value);
	g_free(self);
}
