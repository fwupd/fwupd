/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <efivar.h>
#include <gio/gio.h>

#include "fu-common.h"
#include "fu-uefi-common.h"

#include "fwupd-common.h"

gboolean
fu_uefi_get_bitmap_size (const guint8 *buf,
			 gsize bufsz,
			 guint32 *width,
			 guint32 *height,
			 GError **error)
{
	guint32 ui32;

	g_return_val_if_fail (buf != NULL, FALSE);

	/* check header */
	if (bufsz < 26) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "blob was too small %" G_GSIZE_FORMAT, bufsz);
		return FALSE;
	}
	if (memcmp (buf, "BM", 2) != 0) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "invalid BMP header signature");
		return FALSE;
	}

	/* starting address */
	ui32 = fu_common_read_uint32 (buf + 10, G_LITTLE_ENDIAN);
	if (ui32 < 26) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "BMP header invalid @ %"G_GUINT32_FORMAT"x", ui32);
		return FALSE;
	}

	/* BITMAPINFOHEADER header */
	ui32 = fu_common_read_uint32 (buf + 14, G_LITTLE_ENDIAN);
	if (ui32 < 26 - 14) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "BITMAPINFOHEADER invalid @ %"G_GUINT32_FORMAT"x", ui32);
		return FALSE;
	}

	/* dimensions */
	if (width != NULL)
		*width = fu_common_read_uint32 (buf + 18, G_LITTLE_ENDIAN);
	if (height != NULL)
		*height = fu_common_read_uint32 (buf + 22, G_LITTLE_ENDIAN);
	return TRUE;
}

gboolean
fu_uefi_secure_boot_enabled (void)
{
	gint rc;
	gsize data_size = 0;
	guint32 attributes = 0;
	g_autofree guint8 *data = NULL;

	rc = efi_get_variable (efi_guid_global, "SecureBoot", &data, &data_size, &attributes);
	if (rc < 0)
		return FALSE;
	if (data_size >= 1 && data[0] & 1)
		return TRUE;
	return FALSE;
}

static gint
fu_uefi_strcmp_sort_cb (gconstpointer a, gconstpointer b)
{
	const gchar *stra = *((const gchar **) a);
	const gchar *strb = *((const gchar **) b);
	return g_strcmp0 (stra, strb);
}

GPtrArray *
fu_uefi_get_esrt_entry_paths (const gchar *esrt_path, GError **error)
{
	GPtrArray *entries = g_ptr_array_new_with_free_func (g_free);
	const gchar *fn;
	g_autofree gchar *esrt_entries = NULL;
	g_autoptr(GDir) dir = NULL;

	/* search ESRT */
	esrt_entries = g_build_filename (esrt_path, "entries", NULL);
	dir = g_dir_open (esrt_entries, 0, error);
	if (dir == NULL)
		return NULL;
	while ((fn = g_dir_read_name (dir)) != NULL)
		g_ptr_array_add (entries, g_build_filename (esrt_entries, fn, NULL));

	/* sort by name */
	g_ptr_array_sort (entries, fu_uefi_strcmp_sort_cb);
	return entries;
}

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

