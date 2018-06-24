/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2015-2017 Peter Jones <pjones@redhat.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <gio/gio.h>

#include "fu-common.h"
#include "fu-uefi-vars.h"

//#include "fwupd-common.h"
#include "fwupd-error.h"

static gchar *
fu_uefi_vars_get_path (void)
{
	g_autofree gchar *sysfsfwdir = fu_common_get_path (FU_PATH_KIND_SYSFSDIR_FW);
	return g_build_filename (sysfsfwdir, "efi", "efivars", NULL);
}

static gchar *
fu_uefi_vars_get_filename (const gchar *guid, const gchar *name)
{
	g_autofree gchar *efivardir = fu_uefi_vars_get_path ();
	return g_strdup_printf ("%s/%s-%s", efivardir, name, guid);
}

gboolean
fu_uefi_vars_supported (GError **error)
{
	g_autofree gchar *efivardir = fu_uefi_vars_get_path ();
	if (!g_file_test (efivardir, G_FILE_TEST_IS_DIR)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "kernel efivars support missing: %s",
			     efivardir);
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_uefi_vars_exists (const gchar *guid, const gchar *name)
{
	g_autofree gchar *fn = fu_uefi_vars_get_filename (guid, name);
	return g_file_test (fn, G_FILE_TEST_EXISTS);
}

gboolean
fu_uefi_vars_get_data (const gchar *guid, const gchar *name,
		       guint8 **data, gsize *sz, GError **error)
{
	g_autofree gchar *fn = fu_uefi_vars_get_filename (guid, name);
	g_autofree gchar *data_tmp = NULL;
	if (!g_file_get_contents (fn, &data_tmp, sz, error))
		return FALSE;
	if (data != NULL)
		*data = g_steal_pointer (&data_tmp);
	return TRUE;
}

gboolean
fu_uefi_vars_set_data (const gchar *guid, const gchar *name,
		       const guint8 *data, gsize sz, GError **error)
{
	g_autofree gchar *fn = fu_uefi_vars_get_filename (guid, name);
	g_autoptr(GFile) file = g_file_new_for_path (fn);
	g_autoptr(GFileOutputStream) stream = NULL;
	stream = g_file_replace (file, NULL, FALSE, G_FILE_CREATE_NONE, NULL, error);
	if (stream == NULL)
		return FALSE;
	return g_output_stream_write (G_OUTPUT_STREAM (stream), data, sz, NULL, error) != -1;
}
