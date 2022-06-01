/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2015 Peter Jones <pjones@redhat.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fwupd-error.h"

#include "fu-efivar-impl.h"

/**
 * fu_efivar_supported:
 * @error: #GError
 *
 * Determines if the kernel supports EFI variables
 *
 * Returns: %TRUE on success
 *
 * Since: 1.4.0
 **/
gboolean
fu_efivar_supported(GError **error)
{
	return fu_efivar_supported_impl(error);
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
 * Since: 1.4.0
 **/
gboolean
fu_efivar_delete(const gchar *guid, const gchar *name, GError **error)
{
	g_return_val_if_fail(guid != NULL, FALSE);
	g_return_val_if_fail(name != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	return fu_efivar_delete_impl(guid, name, error);
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
 * Since: 1.4.0
 **/
gboolean
fu_efivar_delete_with_glob(const gchar *guid, const gchar *name_glob, GError **error)
{
	g_return_val_if_fail(guid != NULL, FALSE);
	g_return_val_if_fail(name_glob != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	return fu_efivar_delete_with_glob_impl(guid, name_glob, error);
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
 * Since: 1.4.0
 **/
gboolean
fu_efivar_exists(const gchar *guid, const gchar *name)
{
	g_return_val_if_fail(guid != NULL, FALSE);
	return fu_efivar_exists_impl(guid, name);
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
 * Since: 1.4.0
 **/
gboolean
fu_efivar_get_data(const gchar *guid,
		   const gchar *name,
		   guint8 **data,
		   gsize *data_sz,
		   guint32 *attr,
		   GError **error)
{
	g_return_val_if_fail(guid != NULL, FALSE);
	g_return_val_if_fail(name != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	return fu_efivar_get_data_impl(guid, name, data, data_sz, attr, error);
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
 * Since: 1.5.0
 **/
GBytes *
fu_efivar_get_data_bytes(const gchar *guid, const gchar *name, guint32 *attr, GError **error)
{
	guint8 *data = NULL;
	gsize datasz = 0;

	g_return_val_if_fail(guid != NULL, NULL);
	g_return_val_if_fail(name != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	if (!fu_efivar_get_data(guid, name, &data, &datasz, attr, error))
		return NULL;
	return g_bytes_new_take(data, datasz);
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
 * Since: 1.4.7
 **/
GPtrArray *
fu_efivar_get_names(const gchar *guid, GError **error)
{
	g_return_val_if_fail(guid != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);
	return fu_efivar_get_names_impl(guid, error);
}

/**
 * fu_efivar_get_monitor:
 * @guid: Globally unique identifier
 * @name: Variable name
 * @error: (nullable): optional return location for an error
 *
 * Returns a file monitor for a specific key.
 *
 * Returns: (transfer full): a #GFileMonitor, or %NULL for an error
 *
 * Since: 1.5.5
 **/
GFileMonitor *
fu_efivar_get_monitor(const gchar *guid, const gchar *name, GError **error)
{
	g_return_val_if_fail(guid != NULL, NULL);
	g_return_val_if_fail(name != NULL, NULL);
	return fu_efivar_get_monitor_impl(guid, name, error);
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
 * Since: 1.5.1
 **/
guint64
fu_efivar_space_used(GError **error)
{
	g_return_val_if_fail(error == NULL || *error == NULL, G_MAXUINT64);
	return fu_efivar_space_used_impl(error);
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
 * Since: 1.4.0
 **/
gboolean
fu_efivar_set_data(const gchar *guid,
		   const gchar *name,
		   const guint8 *data,
		   gsize sz,
		   guint32 attr,
		   GError **error)
{
	g_return_val_if_fail(guid != NULL, FALSE);
	g_return_val_if_fail(name != NULL, FALSE);
	g_return_val_if_fail(data != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	return fu_efivar_set_data_impl(guid, name, data, sz, attr, error);
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
 * Since: 1.5.0
 **/
gboolean
fu_efivar_set_data_bytes(const gchar *guid,
			 const gchar *name,
			 GBytes *bytes,
			 guint32 attr,
			 GError **error)
{
	gsize bufsz = 0;
	const guint8 *buf;

	g_return_val_if_fail(guid != NULL, FALSE);
	g_return_val_if_fail(name != NULL, FALSE);
	g_return_val_if_fail(bytes != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	buf = g_bytes_get_data(bytes, &bufsz);
	return fu_efivar_set_data(guid, name, buf, bufsz, attr, error);
}

/**
 * fu_efivar_secure_boot_enabled:
 * @error: (nullable): optional return location for an error
 *
 * Determines if secure boot was enabled
 *
 * Returns: %TRUE on success
 *
 * Since: 1.8.2
 **/
gboolean
fu_efivar_secure_boot_enabled(GError **error)
{
	gsize data_size = 0;
	g_autofree guint8 *data = NULL;

	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (!fu_efivar_get_data(FU_EFIVAR_GUID_EFI_GLOBAL,
				"SecureBoot",
				&data,
				&data_size,
				NULL,
				NULL)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "SecureBoot is not available");
		return FALSE;
	}
	if (data_size >= 1 && data[0] & 1)
		return TRUE;

	/* available, but not enabled */
	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, "SecureBoot is not enabled");
	return FALSE;
}
