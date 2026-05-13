/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fwupd-json-node.h"

FwupdJsonNode *
fwupd_json_node_new_null_internal(void) G_GNUC_WARN_UNUSED_RESULT;
FwupdJsonNode *
fwupd_json_node_new_raw_internal(GRefString *value) G_GNUC_NON_NULL(1) G_GNUC_WARN_UNUSED_RESULT;
FwupdJsonNode *
fwupd_json_node_new_string_internal(GRefString *value) G_GNUC_WARN_UNUSED_RESULT;
void
fwupd_json_node_append_string(FwupdJsonNode *self,
			      GString *str,
			      guint depth,
			      FwupdJsonExportFlags flags) G_GNUC_NON_NULL(1, 2);
