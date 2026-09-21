/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

gboolean
fu_amd_afc_config_id(GByteArray *strings,
		     guint16 *count,
		     const gchar *value,
		     guint16 *id,
		     GError **error);
GPtrArray *
fu_amd_afc_config_parse(GBytes *bytes, GError **error);
gboolean
fu_amd_afc_config_paths_equal(GPtrArray *path1, GPtrArray *path2);
void
fu_amd_afc_config_append_id(GByteArray *entries, guint16 value);
