/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2021 Norbert Kamiński <norbert.kaminski@3mdeb.com>
 * Copyright (C) 2021 Michał Kopeć <michal.kopec@3mdeb.com>
 * Copyright (C) 2021 Sergii Dmytruk <sergii.dmytruk@3mdeb.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <efivar.h>

#include "fu-common.h"
#include "fu-efivar-impl.h"

#include "fwupd-error.h"

gboolean
fu_efivar_supported_impl (GError **error)
{
	if (efi_variables_supported () == 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "kernel efivars support missing");
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_efivar_delete_impl (const gchar *guid, const gchar *name, GError **error)
{
	efi_guid_t guidt;
	efi_str_to_guid (guid, &guidt);

	if (efi_del_variable (guidt, name) == 0)
		return TRUE;

	g_set_error (error,
		     G_IO_ERROR,
		     G_IO_ERROR_INVALID_DATA,
		     "failed to delete efivar %s", name);
	return FALSE;
}

gboolean
fu_efivar_delete_with_glob_impl (const gchar *guid, const gchar *name_glob, GError **error)
{
	efi_guid_t *guidt = NULL;
	gchar *name = NULL;
	gboolean rv = FALSE;
	efi_guid_t guid_to_delete;

	efi_str_to_guid (guid, &guid_to_delete);

	while (efi_get_next_variable_name (&guidt, &name)) {
		if (memcmp (&guid_to_delete, guidt, sizeof (guid_to_delete)) != 0)
			continue;
		if (!fu_common_fnmatch (name, name_glob))
			continue;
		rv = fu_efivar_delete (guid, name, error);
		if (!rv)
			break;
	}
	return rv;
}

static gboolean
fu_efivar_exists_guid (const gchar *guid)
{
	efi_guid_t *guidt = NULL;
	gchar *name = NULL;
	efi_guid_t test;

	efi_str_to_guid (guid, &test);
	while (efi_get_next_variable_name (&guidt, &name)) {
		if (memcmp (&test, guidt, sizeof (test)) == 0) {
			return TRUE;
		}
	}
	return FALSE;
}

gboolean
fu_efivar_exists_impl (const gchar *guid, const gchar *name)
{
	/* any name */
	if (name == NULL)
		return fu_efivar_exists_guid (guid);

	return fu_efivar_get_data (guid, name, NULL, NULL, NULL, NULL);
}

gboolean
fu_efivar_get_data_impl (const gchar *guid, const gchar *name, guint8 **data,
			 gsize *data_sz, guint32 *attr, GError **error)
{
	efi_guid_t guidt;
	efi_str_to_guid (guid, &guidt);
	return (efi_get_variable (guidt, name, data, data_sz, attr) != 0);
}


GPtrArray *
fu_efivar_get_names_impl (const gchar *guid, GError **error)
{
	g_autoptr(GPtrArray) names = g_ptr_array_new_with_free_func (g_free);
	efi_guid_t *guidt = NULL;
	gchar *name = NULL;
	efi_guid_t test;
	efi_str_to_guid (guid, &test);

	/* find names with matching GUID */
	while (efi_get_next_variable_name (&guidt, &name)) {
		if (memcmp (&test, guidt, sizeof (test)) == 0) {
			g_ptr_array_add (names, g_strdup (name));
		}
	}

	/* nothing found */
	if (names->len == 0) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_FOUND,
			     "no names for GUID %s", guid);
		return NULL;
	}

	/* success */
	return g_steal_pointer (&names);
}

GFileMonitor *
fu_efivar_get_monitor_impl (const gchar *guid, const gchar *name, GError **error)
{
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "efivarfs monitoring not supported on FreeBSD");
	return NULL;
}

guint64
fu_efivar_space_used_impl (GError **error)
{
	guint64 total = 0;
	efi_guid_t *guidt = NULL;
	char *name = NULL;

	while (efi_get_next_variable_name (&guidt, &name)) {
		size_t size = 0;
		if (efi_get_variable_size (*guidt, name, &size) < 0) {
			g_set_error (error,
					G_IO_ERROR,
					G_IO_ERROR_FAILED,
					"failed to get efivar size");
			return G_MAXUINT64;
		}
		total += size;
	}

	/* success */
	return total;
}

gboolean
fu_efivar_set_data_impl (const gchar *guid, const gchar *name, const guint8 *data,
			 gsize sz, guint32 attr, GError **error)
{
	efi_guid_t guidt;
	efi_str_to_guid (guid, &guidt);

	if (efi_set_variable (guidt, name, (guint8 *)data, sz, attr) != 0) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "failed to write data to efivar %s", name);
		return FALSE;
	}

	/* success */
	return TRUE;
}
