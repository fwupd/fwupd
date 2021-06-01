/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2021 Norbert Kamiński <norbert.kaminski@3mdeb.com>
 * Copyright (C) 2021 Michał Kopeć <michal.kopec@3mdeb.com>
 * Copyright (C) 2021 Sergii Dmytruk <sergii.dmytruk@3mdeb.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <efivar.h>

#include "fu-common.h"
#include "fu-efivar.h"

#include "fwupd-error.h"

/**
 * fu_efivar_supported:
 * @error: #GError
 *
 * Determines if the kernel supports EFI variables
 *
 * Returns: %TRUE on success
 *
 * Since: 1.6.1
 **/
gboolean
fu_efivar_supported (GError **error)
{
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (efi_variables_supported () == 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "kernel efivars support missing");
		return FALSE;
	}
	return TRUE;
}

/**
 * fu_efivar_space_used:
 * @error: (nullable): optional return location for an error
 *
 * Gets the total size used by all EFI variables. This may be less than the size reported by the
 * kernel as some (hopefully small) variables are hidden from userspace.
 *
 * Returns: total allocated size of all visible variables, or %G_MAXUINT64 on error
 *
 * Since: 1.6.1
 **/
guint64
fu_efivar_space_used (GError **error)
{
	g_return_val_if_fail (error == NULL || *error == NULL, G_MAXUINT64);

	guint64 total = 0;
	efi_guid_t *guid = NULL;
	char *name = NULL;

	while (efi_get_next_variable_name (&guid, &name)) {
		size_t size = 0;
		if (efi_get_variable_size (*guid, name, &size) < 0) {
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

/**
 * fu_efivar_set_data:
 * @guid: Globally unique identifier
 * @name: Variable name
 * @data: Data to set
 * @sz: size of @data
 * @attr: Attributes
 * @error: (nullable): optional return location for an error
 *
 * Sets the data to a UEFI variable in NVRAM
 *
 * Returns: %TRUE on success
 *
 * Since: 1.6.1
 **/
gboolean
fu_efivar_set_data (const gchar *guids, const gchar *name, const guint8 *data,
		     gsize sz, guint32 attr, GError **error)
{
	g_return_val_if_fail (guids != NULL, FALSE);
	g_return_val_if_fail (name != NULL, FALSE);
	g_return_val_if_fail (data != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	efi_guid_t guid;
	efi_str_to_guid (guids, &guid);

	if (efi_set_variable (guid, name, data, sz, attr) != 0) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "failed to write data to efivar %s", name);
		return FALSE;
	}

	/* success */
	return TRUE;
}

/**
 * fu_efivar_delete:
 * @guid: Globally unique identifier
 * @name: Variable name
 * @error: #GError
 *
 * Removes a variable from NVRAM
 *
 * Returns: %TRUE on success
 *
 * Since: 1.6.1
 **/
gboolean
fu_efivar_delete (const gchar *guids, const gchar *name, GError **error)
{
	g_return_val_if_fail (guids != NULL, FALSE);
	g_return_val_if_fail (name != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	efi_guid_t guid;
	efi_str_to_guid (guids, &guid);

	if (efi_del_variable (guid, name) == 0)
		return TRUE;

	g_set_error (error,
		     G_IO_ERROR,
		     G_IO_ERROR_INVALID_DATA,
		     "failed to delete efivar %s", name);
	return FALSE;
}

/**
 * fu_efivar_delete_with_glob:
 * @guid: Globally unique identifier
 * @name_glob: Variable name
 * @error: #GError
 *
 * Removes a group of variables from NVRAM
 *
 * Returns: %TRUE on success
 *
 * Since: 1.6.1
 **/
gboolean
fu_efivar_delete_with_glob (const gchar *guids, const gchar *name_glob, GError **error)
{
	g_return_val_if_fail (guids != NULL, FALSE);
	g_return_val_if_fail (name_glob != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	efi_guid_t *guid = NULL;
	gchar *name = NULL;

	gboolean rv = FALSE;

	efi_guid_t guid_to_delete;
	efi_str_to_guid (guids, &guid_to_delete);

	while (efi_get_next_variable_name (&guid, &name)) {
		if (memcmp (&guid_to_delete, guid, sizeof (guid)) != 0)
			continue;
		if (!fu_common_fnmatch (name, name_glob))
			continue;
		rv = fu_efivar_delete (guids, name, error);
		if (!rv)
			break;
	}
	return rv;
}

static gboolean
fu_efivar_exists_guid (const gchar *guids)
{
	efi_guid_t *guid = NULL;
	gchar *name = NULL;
	efi_guid_t test;
	efi_str_to_guid (guids, &test);

	while (efi_get_next_variable_name (&guid, &name)) {
		if (memcmp (&test, guid, sizeof (test)) == 0) {
			return TRUE;
		}
	}
	return FALSE;
}

/**
 * fu_efivar_exists:
 * @guid: Globally unique identifier
 * @name: (nullable): Variable name
 *
 * Test if a variable exists
 *
 * Returns: %TRUE on success
 *
 * Since: 1.6.1
 **/
gboolean
fu_efivar_exists (const gchar *guids, const gchar *name)
{
	g_return_val_if_fail (guids != NULL, FALSE);

	/* any name */
	if (name == NULL)
		return fu_efivar_exists_guid (guids);

	return fu_efivar_get_data (guids, name, NULL, NULL, NULL, NULL);
}

/**
 * fu_efivar_get_data:
 * @guid: Globally unique identifier
 * @name: Variable name
 * @data: Data to set
 * @data_sz: size of data
 * @attr: Attributes
 * @error: (nullable): optional return location for an error
 *
 * Gets the data from a UEFI variable in NVRAM
 *
 * Returns: %TRUE on success
 *
 * Since: 1.6.1
 **/
gboolean
fu_efivar_get_data (const gchar *guids, const gchar *name, guint8 **data,
		       gsize *data_sz, guint32 *attr, GError **error)
{
	g_return_val_if_fail (guids != NULL, FALSE);
	g_return_val_if_fail (name != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	efi_guid_t guid;
	efi_str_to_guid (guids, &guid);

	return (efi_get_variable (guid, name, data, data_sz, attr) != 0);
}

/**
 * fu_efivar_get_data_bytes:
 * @guid: Globally unique identifier
 * @name: Variable name
 * @attr: (nullable): Attributes
 * @error: (nullable): optional return location for an error
 *
 * Gets the data from a UEFI variable in NVRAM
 *
 * Returns: (transfer full): a #GBytes, or %NULL
 *
 * Since: 1.6.1
 **/
GBytes *
fu_efivar_get_data_bytes (const gchar *guid,
			  const gchar *name,
			  guint32 *attr,
			  GError **error)
{
	g_return_val_if_fail (guid != NULL, NULL);
	g_return_val_if_fail (name != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	guint8 *data = NULL;
	gsize datasz = 0;

	if (!fu_efivar_get_data (guid, name, &data, &datasz, attr, error))
		return NULL;
	return g_bytes_new_take (data, datasz);
}

/**
 * fu_efivar_get_names:
 * @guid: Globally unique identifier
 * @error: (nullable): optional return location for an error
 *
 * Gets the list of names where the GUID matches. An error is set if there are
 * no names matching the GUID.
 *
 * Returns: (transfer container) (element-type utf8): array of names
 *
 * Since: 1.6.1
 **/
GPtrArray *
fu_efivar_get_names (const gchar *guids, GError **error)
{
	g_return_val_if_fail (guids != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	const gchar *name_guid;
	g_autoptr(GPtrArray) names = g_ptr_array_new_with_free_func (g_free);

	/* find names with matching GUID */
	efi_guid_t *guid = NULL;
	gchar *name = NULL;
	efi_guid_t test;
	efi_str_to_guid (guids, &test);

	while (efi_get_next_variable_name (&guid, &name)) {
		if (memcmp (&test, guid, sizeof (test)) == 0) {
			g_ptr_array_add (names, g_strdup (name));
		}
	}

	/* nothing found */
	if (names->len == 0) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_FOUND,
			     "no names for GUID %s", guids);
		return NULL;
	}

	/* success */
	return g_steal_pointer (&names);
}

/**
 * fu_efivar_set_data_bytes:
 * @guid: globally unique identifier
 * @name: variable name
 * @bytes: data blob
 * @attr: attributes
 * @error: (nullable): optional return location for an error
 *
 * Sets the data to a UEFI variable in NVRAM
 *
 * Returns: %TRUE on success
 *
 * Since: 1.6.1
 **/
gboolean
fu_efivar_set_data_bytes (const gchar *guid, const gchar *name, GBytes *bytes,
			  guint32 attr, GError **error)
{
	gsize bufsz = 0;
	const guint8 *buf;

	g_return_val_if_fail (guid != NULL, FALSE);
	g_return_val_if_fail (name != NULL, FALSE);
	g_return_val_if_fail (bytes != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	buf = g_bytes_get_data (bytes, &bufsz);
	return fu_efivar_set_data (guid, name, buf, bufsz, attr, error);
}

/**
 * fu_efivar_secure_boot_enabled_full:
 * @error: (nullable): optional return location for an error
 *
 * Determines if secure boot was enabled
 *
 * Returns: %TRUE on success
 *
 * Since: 1.6.1
 **/
gboolean
fu_efivar_secure_boot_enabled_full (GError **error)
{
	gsize data_size = 0;
	g_autofree guint8 *data = NULL;

	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (!fu_efivar_get_data (FU_EFIVAR_GUID_EFI_GLOBAL, "SecureBoot",
				 &data, &data_size, NULL, NULL)) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "SecureBoot is not available");
		return FALSE;
	}
	if (data_size >= 1 && data[0] & 1)
		return TRUE;

	/* available, but not enabled */
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_FOUND,
			     "SecureBoot is not enabled");
	return FALSE;
}

/**
 * fu_efivar_secure_boot_enabled:
 *
 * Determines if secure boot was enabled
 *
 * Returns: %TRUE on success
 *
 * Since: 1.6.1
 **/
gboolean
fu_efivar_secure_boot_enabled (void)
{
	return fu_efivar_secure_boot_enabled_full (NULL);
}
