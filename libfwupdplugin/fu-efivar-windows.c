/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2015 Peter Jones <pjones@redhat.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-efivar-impl.h"

#include "fwupd-error.h"

gboolean
fu_efivar_supported_impl (GError **error)
{
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "efivarfs not currently supported on Windows");
	return FALSE;
}

gboolean
fu_efivar_delete_impl (const gchar *guid, const gchar *name, GError **error)
{
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "efivarfs not currently supported on Windows");
	return FALSE;
}

gboolean
fu_efivar_delete_with_glob_impl (const gchar *guid, const gchar *name_glob, GError **error)
{
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "efivarfs not currently supported on Windows");
	return FALSE;
}

gboolean
fu_efivar_exists_impl (const gchar *guid, const gchar *name)
{
	return FALSE;
}

gboolean
fu_efivar_get_data_impl (const gchar *guid, const gchar *name, guint8 **data,
			 gsize *data_sz, guint32 *attr, GError **error)
{
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "efivarfs not currently supported on Windows");
	return FALSE;
}

GPtrArray *
fu_efivar_get_names_impl (const gchar *guid, GError **error)
{
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "efivarfs not currently supported on Windows");
	return NULL;
}

GFileMonitor *
fu_efivar_get_monitor_impl (const gchar *guid, const gchar *name, GError **error)
{
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "efivarfs not currently supported on Windows");
	return NULL;
}

guint64
fu_efivar_space_used_impl (GError **error)
{
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "efivarfs not currently supported on Windows");
	return G_MAXUINT64;
}

gboolean
fu_efivar_set_data_impl (const gchar *guid, const gchar *name, const guint8 *data,
			 gsize sz, guint32 attr, GError **error)
{
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "efivarfs not currently supported on Windows");
	return FALSE;
}
