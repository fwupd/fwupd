/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-common.h"
#include "fu-uefi-dbx-common.h"

gchar *
fu_uefi_dbx_get_dbxupdate (GError **error)
{
	g_autofree gchar *dbxdir = NULL;
	g_autofree gchar *glob = NULL;
	g_autoptr(GPtrArray) files = NULL;

	/* get the newest files from dbxtool, prefer the per-arch ones first */
	dbxdir = fu_common_get_path (FU_PATH_KIND_EFIDBXDIR);
	glob = g_strdup_printf ("*%s*.bin", EFI_MACHINE_TYPE_NAME);
	files = fu_common_filename_glob (dbxdir, glob, NULL);
	if (files == NULL)
		files = fu_common_filename_glob (dbxdir, "*.bin", error);
	if (files == NULL)
		return NULL;
	return g_strdup (g_ptr_array_index (files, 0));
}
