/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fwupd-json-object.h"

void
fwupd_json_object_append_string(FwupdJsonObject *self,
				GString *str,
				guint depth,
				FwupdJsonExportFlags flags) G_GNUC_NON_NULL(1, 2);
void
fwupd_json_object_add_raw_internal(FwupdJsonObject *self,
				   GRefString *key,
				   GRefString *value,
				   FwupdJsonLoadFlags flags) G_GNUC_NON_NULL(1, 2, 3);
void
fwupd_json_object_add_string_internal(FwupdJsonObject *self,
				      GRefString *key,
				      GRefString *value,
				      FwupdJsonLoadFlags flags) G_GNUC_NON_NULL(1, 2, 3);
void
fwupd_json_object_add_object_internal(FwupdJsonObject *self,
				      GRefString *key,
				      FwupdJsonObject *json_obj) G_GNUC_NON_NULL(1, 2, 3);
void
fwupd_json_object_add_array_internal(FwupdJsonObject *self,
				     GRefString *key,
				     FwupdJsonArray *json_arr) G_GNUC_NON_NULL(1, 2, 3);
