/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fwupd-json-array.h"

G_BEGIN_DECLS

void
fwupd_json_array_append_string(FwupdJsonArray *self,
			       GString *str,
			       guint depth,
			       FwupdJsonExportFlags flags) G_GNUC_NON_NULL(1, 2);
void
fwupd_json_array_add_string_internal(FwupdJsonArray *self, GRefString *value) G_GNUC_NON_NULL(1, 2);
void
fwupd_json_array_add_raw_internal(FwupdJsonArray *self, GRefString *value) G_GNUC_NON_NULL(1, 2);

G_END_DECLS
