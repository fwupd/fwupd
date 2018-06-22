/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-uefi-common.h"

guint64
fu_uefi_read_file_as_uint64 (const gchar *path, const gchar *attr_name)
{
	g_autofree gchar *data = NULL;
	g_autofree gchar *fn = g_build_filename (path, attr_name, NULL);
	if (!g_file_get_contents (fn, &data, NULL, NULL))
		return 0x0;
	if (g_str_has_prefix (data, "0x"))
		return g_ascii_strtoull (data + 2, NULL, 16);
	return g_ascii_strtoull (data, NULL, 10);
}

