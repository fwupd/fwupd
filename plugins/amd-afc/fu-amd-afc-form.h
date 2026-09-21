/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

typedef struct {
	gchar *name;
	guint16 id;
	GArray *settings; /* guint */
	GArray *refs;	  /* guint16 */
	gboolean visited;
} FuAmdAfcForm;

void
fu_amd_afc_form_export(FuAmdAfcForm *self, FuFirmwareExportFlags flags, XbBuilderNode *bn);
void
fu_amd_afc_form_free(FuAmdAfcForm *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuAmdAfcForm, fu_amd_afc_form_free)
