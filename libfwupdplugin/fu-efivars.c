/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 * Copyright 2015 Peter Jones <pjones@redhat.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fwupd-error.h"

#include "fu-efivars.h"

G_DEFINE_TYPE(FuEfivars, fu_efivars, G_TYPE_OBJECT)

/**
 * fu_efivars_supported:
 * @self: a #FuEfivars
 * @error: #GError
 *
 * Determines if the kernel supports EFI variables
 *
 * Returns: %TRUE on success
 *
 * Since: 2.0.0
 **/
gboolean
fu_efivars_supported(FuEfivars *self, GError **error)
{
	FuEfivarsClass *efivars_class = FU_EFIVARS_GET_CLASS(self);

	g_return_val_if_fail(FU_IS_EFIVARS(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (efivars_class->supported == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "not supported");
		return FALSE;
	}
	return efivars_class->supported(self, error);
}

/**
 * fu_efivars_delete:
 * @self: a #FuEfivars
 * @guid: Globally unique identifier
 * @name: Variable name
 * @error: #GError
 *
 * Removes a variable from NVRAM, returning an error if it does not exist.
 *
 * Returns: %TRUE on success
 *
 * Since: 2.0.0
 **/
gboolean
fu_efivars_delete(FuEfivars *self, const gchar *guid, const gchar *name, GError **error)
{
	FuEfivarsClass *efivars_class = FU_EFIVARS_GET_CLASS(self);

	g_return_val_if_fail(FU_IS_EFIVARS(self), FALSE);
	g_return_val_if_fail(guid != NULL, FALSE);
	g_return_val_if_fail(name != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (efivars_class->delete == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "not supported");
		return FALSE;
	}
	return efivars_class->delete (self, guid, name, error);
}

/**
 * fu_efivars_delete_with_glob:
 * @self: a #FuEfivars
 * @guid: Globally unique identifier
 * @name_glob: Variable name
 * @error: #GError
 *
 * Removes a group of variables from NVRAM
 *
 * Returns: %TRUE on success
 *
 * Since: 2.0.0
 **/
gboolean
fu_efivars_delete_with_glob(FuEfivars *self,
			    const gchar *guid,
			    const gchar *name_glob,
			    GError **error)
{
	FuEfivarsClass *efivars_class = FU_EFIVARS_GET_CLASS(self);

	g_return_val_if_fail(FU_IS_EFIVARS(self), FALSE);
	g_return_val_if_fail(guid != NULL, FALSE);
	g_return_val_if_fail(name_glob != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (efivars_class->delete_with_glob == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "not supported");
		return FALSE;
	}
	return efivars_class->delete_with_glob(self, guid, name_glob, error);
}

/**
 * fu_efivars_exists:
 * @self: a #FuEfivars
 * @guid: Globally unique identifier
 * @name: (nullable): Variable name
 *
 * Test if a variable exists
 *
 * Returns: %TRUE on success
 *
 * Since: 2.0.0
 **/
gboolean
fu_efivars_exists(FuEfivars *self, const gchar *guid, const gchar *name)
{
	FuEfivarsClass *efivars_class = FU_EFIVARS_GET_CLASS(self);

	g_return_val_if_fail(FU_IS_EFIVARS(self), FALSE);
	g_return_val_if_fail(guid != NULL, FALSE);

	if (efivars_class->exists == NULL)
		return FALSE;
	return efivars_class->exists(self, guid, name);
}

/**
 * fu_efivars_get_data:
 * @self: a #FuEfivars
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
 * Since: 2.0.0
 **/
gboolean
fu_efivars_get_data(FuEfivars *self,
		    const gchar *guid,
		    const gchar *name,
		    guint8 **data,
		    gsize *data_sz,
		    guint32 *attr,
		    GError **error)
{
	FuEfivarsClass *efivars_class = FU_EFIVARS_GET_CLASS(self);

	g_return_val_if_fail(FU_IS_EFIVARS(self), FALSE);
	g_return_val_if_fail(guid != NULL, FALSE);
	g_return_val_if_fail(name != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (efivars_class->get_data == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "not supported");
		return FALSE;
	}
	return efivars_class->get_data(self, guid, name, data, data_sz, attr, error);
}

/**
 * fu_efivars_get_data_bytes:
 * @self: a #FuEfivars
 * @guid: Globally unique identifier
 * @name: Variable name
 * @attr: (nullable): Attributes
 * @error: (nullable): optional return location for an error
 *
 * Gets the data from a UEFI variable in NVRAM
 *
 * Returns: (transfer full): a #GBytes, or %NULL
 *
 * Since: 2.0.0
 **/
GBytes *
fu_efivars_get_data_bytes(FuEfivars *self,
			  const gchar *guid,
			  const gchar *name,
			  guint32 *attr,
			  GError **error)
{
	guint8 *data = NULL;
	gsize datasz = 0;

	g_return_val_if_fail(FU_IS_EFIVARS(self), NULL);
	g_return_val_if_fail(guid != NULL, NULL);
	g_return_val_if_fail(name != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	if (!fu_efivars_get_data(self, guid, name, &data, &datasz, attr, error))
		return NULL;
	return g_bytes_new_take(data, datasz);
}

/**
 * fu_efivars_get_names:
 * @self: a #FuEfivars
 * @guid: Globally unique identifier
 * @error: (nullable): optional return location for an error
 *
 * Gets the list of names where the GUID matches. An error is set if there are
 * no names matching the GUID.
 *
 * Returns: (transfer container) (element-type utf8): array of names
 *
 * Since: 2.0.0
 **/
GPtrArray *
fu_efivars_get_names(FuEfivars *self, const gchar *guid, GError **error)
{
	FuEfivarsClass *efivars_class = FU_EFIVARS_GET_CLASS(self);

	g_return_val_if_fail(FU_IS_EFIVARS(self), NULL);
	g_return_val_if_fail(guid != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	if (efivars_class->get_names == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "not supported");
		return NULL;
	}
	return efivars_class->get_names(self, guid, error);
}

/**
 * fu_efivars_get_monitor:
 * @self: a #FuEfivars
 * @guid: Globally unique identifier
 * @name: Variable name
 * @error: (nullable): optional return location for an error
 *
 * Returns a file monitor for a specific key.
 *
 * Returns: (transfer full): a #GFileMonitor, or %NULL for an error
 *
 * Since: 2.0.0
 **/
GFileMonitor *
fu_efivars_get_monitor(FuEfivars *self, const gchar *guid, const gchar *name, GError **error)
{
	FuEfivarsClass *efivars_class = FU_EFIVARS_GET_CLASS(self);

	g_return_val_if_fail(FU_IS_EFIVARS(self), NULL);
	g_return_val_if_fail(guid != NULL, NULL);
	g_return_val_if_fail(name != NULL, NULL);

	if (efivars_class->get_monitor == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "not supported");
		return NULL;
	}
	return efivars_class->get_monitor(self, guid, name, error);
}

/**
 * fu_efivars_space_used:
 * @self: a #FuEfivars
 * @error: (nullable): optional return location for an error
 *
 * Gets the total size used by all EFI variables. This may be less than the size reported by the
 * kernel as some (hopefully small) variables are hidden from userspace.
 *
 * Returns: total allocated size of all visible variables, or %G_MAXUINT64 on error
 *
 * Since: 2.0.0
 **/
guint64
fu_efivars_space_used(FuEfivars *self, GError **error)
{
	FuEfivarsClass *efivars_class = FU_EFIVARS_GET_CLASS(self);

	g_return_val_if_fail(FU_IS_EFIVARS(self), G_MAXUINT64);
	g_return_val_if_fail(error == NULL || *error == NULL, G_MAXUINT64);

	if (efivars_class->space_used == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "not supported");
		return G_MAXUINT64;
	}
	return efivars_class->space_used(self, error);
}

/**
 * fu_efivars_set_data:
 * @self: a #FuEfivars
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
 * Since: 2.0.0
 **/
gboolean
fu_efivars_set_data(FuEfivars *self,
		    const gchar *guid,
		    const gchar *name,
		    const guint8 *data,
		    gsize sz,
		    guint32 attr,
		    GError **error)
{
	FuEfivarsClass *efivars_class = FU_EFIVARS_GET_CLASS(self);

	g_return_val_if_fail(FU_IS_EFIVARS(self), FALSE);
	g_return_val_if_fail(guid != NULL, FALSE);
	g_return_val_if_fail(name != NULL, FALSE);
	g_return_val_if_fail(data != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (efivars_class->set_data == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "not supported");
		return FALSE;
	}
	return efivars_class->set_data(self, guid, name, data, sz, attr, error);
}

/**
 * fu_efivars_set_data_bytes:
 * @self: a #FuEfivars
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
 * Since: 2.0.0
 **/
gboolean
fu_efivars_set_data_bytes(FuEfivars *self,
			  const gchar *guid,
			  const gchar *name,
			  GBytes *bytes,
			  guint32 attr,
			  GError **error)
{
	gsize bufsz = 0;
	const guint8 *buf;

	g_return_val_if_fail(FU_IS_EFIVARS(self), FALSE);
	g_return_val_if_fail(guid != NULL, FALSE);
	g_return_val_if_fail(name != NULL, FALSE);
	g_return_val_if_fail(bytes != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	buf = g_bytes_get_data(bytes, &bufsz);
	return fu_efivars_set_data(self, guid, name, buf, bufsz, attr, error);
}

/**
 * fu_efivars_secure_boot_enabled:
 * @self: a #FuEfivars
 * @error: (nullable): optional return location for an error
 *
 * Determines if secure boot was enabled
 *
 * Returns: %TRUE on success
 *
 * Since: 2.0.0
 **/
gboolean
fu_efivars_secure_boot_enabled(FuEfivars *self, GError **error)
{
	gsize data_size = 0;
	g_autofree guint8 *data = NULL;

	g_return_val_if_fail(FU_IS_EFIVARS(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (!fu_efivars_get_data(self,
				 FU_EFIVARS_GUID_EFI_GLOBAL,
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

static void
fu_efivars_init(FuEfivars *self)
{
}

static void
fu_efivars_class_init(FuEfivarsClass *klass)
{
}
