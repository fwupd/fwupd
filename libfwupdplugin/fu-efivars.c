/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 * Copyright 2015 Peter Jones <pjones@redhat.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fwupd-error.h"

#include "fu-byte-array.h"
#include "fu-dummy-efivars.h"
#include "fu-efi-device-path-list.h"
#include "fu-efi-file-path-device-path.h"
#include "fu-efi-hard-drive-device-path.h"
#include "fu-efi-signature-list.h"
#include "fu-efivars-private.h"
#include "fu-mem.h"
#include "fu-pefile-firmware.h"

typedef struct {
	FuPathStore *pstore;
	GHashTable *auth_variables; /* FuEfiSecureBootSiglistKind:FuEfiSignatureList */
} FuEfivarsPrivate;

enum { PROP_0, PROP_PATH_STORE, PROP_LAST };

G_DEFINE_TYPE_WITH_PRIVATE(FuEfivars, fu_efivars, G_TYPE_OBJECT);

#define GET_PRIVATE(o) (fu_efivars_get_instance_private(o))

/**
 * fu_efivars_get_path_store:
 * @self: a #FuEfivars
 *
 * Gets the well-known path store.
 *
 * Returns: (transfer none): a #FuPathStore, or %NULL if not set
 **/
FuPathStore *
fu_efivars_get_path_store(FuEfivars *self)
{
	FuEfivarsPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_EFIVARS(self), NULL);
	return priv->pstore;
}

/**
 * fu_efivars_supported:
 * @self: a #FuEfivars
 * @error: #GError
 *
 * Determines if the kernel supports EFI variables
 *
 * Returns: %TRUE on success
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
	return efivars_class->delete(self, guid, name, error);
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
 * @name: (not nullable): Variable name
 *
 * Test if a variable exists
 *
 * Returns: %TRUE on success
 **/
gboolean
fu_efivars_exists(FuEfivars *self, const gchar *guid, const gchar *name)
{
	FuEfivarsClass *efivars_class = FU_EFIVARS_GET_CLASS(self);

	g_return_val_if_fail(FU_IS_EFIVARS(self), FALSE);
	g_return_val_if_fail(guid != NULL, FALSE);
	g_return_val_if_fail(name != NULL, FALSE);

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
 * @attr: (nullable) (out): #FuEfiVariableAttrs, e.g. %FU_EFI_VARIABLE_ATTR_NON_VOLATILE
 * @error: (nullable): optional return location for an error
 *
 * Gets the data from a UEFI variable in NVRAM
 *
 * Returns: %TRUE on success
 **/
gboolean
fu_efivars_get_data(FuEfivars *self,
		    const gchar *guid,
		    const gchar *name,
		    guint8 **data,
		    gsize *data_sz,
		    FuEfiVariableAttrs *attr,
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
 * fu_efivars_get_attrs:
 * @self: a #FuEfivars
 * @guid: Globally unique identifier
 * @name: Variable name
 * @attrs: (nullable) (out): #FuEfiVariableAttrs, e.g. %FU_EFI_VARIABLE_ATTR_NON_VOLATILE
 * @error: (nullable): optional return location for an error
 *
 * Gets the attributes from a UEFI variable in NVRAM
 *
 * Returns: %TRUE on success
 **/
gboolean
fu_efivars_get_attrs(FuEfivars *self,
		     const gchar *guid,
		     const gchar *name,
		     FuEfiVariableAttrs *attrs,
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
	return efivars_class->get_data(self, guid, name, NULL, NULL, attrs, error);
}

/**
 * fu_efivars_get_data_bytes:
 * @self: a #FuEfivars
 * @guid: Globally unique identifier
 * @name: Variable name
 * @attr: (out): #FuEfiVariableAttrs, e.g. %FU_EFI_VARIABLE_ATTR_NON_VOLATILE
 * @error: (nullable): optional return location for an error
 *
 * Gets the data from a UEFI variable in NVRAM
 *
 * Returns: (transfer full): a #GBytes, or %NULL
 **/
GBytes *
fu_efivars_get_data_bytes(FuEfivars *self,
			  const gchar *guid,
			  const gchar *name,
			  FuEfiVariableAttrs *attr,
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
 **/
GFileMonitor *
fu_efivars_get_monitor(FuEfivars *self, const gchar *guid, const gchar *name, GError **error)
{
	FuEfivarsClass *efivars_class = FU_EFIVARS_GET_CLASS(self);

	g_return_val_if_fail(FU_IS_EFIVARS(self), NULL);
	g_return_val_if_fail(guid != NULL, NULL);
	g_return_val_if_fail(name != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	if (efivars_class->get_monitor == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "not supported");
		return NULL;
	}
	return efivars_class->get_monitor(self, guid, name, error);
}

static const struct {
	FuEfiSecureBootSiglistKind kind;
	const gchar *guid;
	const gchar *name;
	gboolean rw;
} fu_efivars_secure_boot_siglist_map[] = {
    {FU_EFI_SECURE_BOOT_SIGLIST_KIND_PK, FU_EFIVARS_GUID_EFI_GLOBAL, "PK", FALSE},
    {FU_EFI_SECURE_BOOT_SIGLIST_KIND_KEK, FU_EFIVARS_GUID_EFI_GLOBAL, "KEK", TRUE},
    {FU_EFI_SECURE_BOOT_SIGLIST_KIND_DB_DEFAULT, FU_EFIVARS_GUID_EFI_GLOBAL, "dbDefault", FALSE},
    {FU_EFI_SECURE_BOOT_SIGLIST_KIND_DB, FU_EFIVARS_GUID_SECURITY_DATABASE, "db", TRUE},
    {FU_EFI_SECURE_BOOT_SIGLIST_KIND_DBX, FU_EFIVARS_GUID_SECURITY_DATABASE, "dbx", TRUE},
};

static gboolean
fu_efivars_secure_boot_siglist_kind_is_rw(FuEfiSecureBootSiglistKind kind)
{
	for (guint i = 0; i < G_N_ELEMENTS(fu_efivars_secure_boot_siglist_map); i++) {
		if (fu_efivars_secure_boot_siglist_map[i].kind == kind)
			return fu_efivars_secure_boot_siglist_map[i].rw;
	}
	return FALSE;
}

/* private */
const gchar *
fu_efivars_secure_boot_siglist_kind_to_guid(FuEfiSecureBootSiglistKind kind)
{
	for (guint i = 0; i < G_N_ELEMENTS(fu_efivars_secure_boot_siglist_map); i++) {
		if (fu_efivars_secure_boot_siglist_map[i].kind == kind)
			return fu_efivars_secure_boot_siglist_map[i].guid;
	}
	return NULL;
}

/* private */
const gchar *
fu_efivars_secure_boot_siglist_kind_to_name(FuEfiSecureBootSiglistKind kind)
{
	for (guint i = 0; i < G_N_ELEMENTS(fu_efivars_secure_boot_siglist_map); i++) {
		if (fu_efivars_secure_boot_siglist_map[i].kind == kind)
			return fu_efivars_secure_boot_siglist_map[i].name;
	}
	return NULL;
}

/* private */
FuEfiSecureBootSiglistKind
fu_efivars_secure_boot_siglist_kind_from_guid_name(const gchar *guid, const gchar *name)
{
	for (guint i = 0; i < G_N_ELEMENTS(fu_efivars_secure_boot_siglist_map); i++) {
		if (g_strcmp0(fu_efivars_secure_boot_siglist_map[i].guid, guid) == 0 &&
		    g_strcmp0(fu_efivars_secure_boot_siglist_map[i].name, name) == 0)
			return fu_efivars_secure_boot_siglist_map[i].kind;
	}
	return FU_EFI_SECURE_BOOT_SIGLIST_KIND_UNKNOWN;
}

/**
 * fu_efivars_get_secure_boot_siglist:
 * @self: a #FuEfivars
 * @kind: a #FuEfiSecureBootSiglistKind, e.g. %FU_EFI_SECURE_BOOT_SIGLIST_KIND_PK
 * @error: (nullable): optional return location for an error
 *
 * Loads and parses a well-known UEFI Secure Boot variable, returning the cached contents if
 * already parsed.
 *
 * If the variable is missing then an empty #FuEfiSignatureList is returned.
 *
 * Returns: (transfer full): a #FuEfiSignatureList, or %NULL on error
 **/
FuFirmware *
fu_efivars_get_secure_boot_siglist(FuEfivars *self, FuEfiSecureBootSiglistKind kind, GError **error)
{
	FuEfivarsPrivate *priv = GET_PRIVATE(self);
	FuFirmware *siglist_tmp;
	const gchar *guid = fu_efivars_secure_boot_siglist_kind_to_guid(kind);
	const gchar *name = fu_efivars_secure_boot_siglist_kind_to_name(kind);
	g_autoptr(FuFirmware) siglist = fu_efi_signature_list_new();
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail(FU_IS_EFIVARS(self), NULL);
	g_return_val_if_fail(kind != FU_EFI_SECURE_BOOT_SIGLIST_KIND_UNKNOWN, NULL);
	g_return_val_if_fail(guid != NULL, NULL);
	g_return_val_if_fail(name != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* already exists */
	siglist_tmp = g_hash_table_lookup(priv->auth_variables, GUINT_TO_POINTER(kind));
	if (siglist_tmp)
		return g_object_ref(siglist_tmp);

	/* read blob */
	blob = fu_efivars_get_data_bytes(self, guid, name, NULL, &error_local);
	if (blob == NULL) {
		if (!g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND)) {
			g_propagate_error(error, g_steal_pointer(&error_local));
			return NULL;
		}
		g_debug("failed to read %s: %s",
			fu_efi_secure_boot_siglist_kind_to_string(kind),
			error_local->message);
	} else {
		if (!fu_firmware_parse_bytes(siglist,
					     blob,
					     0x0,
					     FU_FIRMWARE_PARSE_FLAG_IGNORE_CHECKSUM,
					     error)) {
			g_prefix_error(error,
				       "failed to parse %s: ",
				       fu_efi_secure_boot_siglist_kind_to_string(kind));
			return NULL;
		}
	}

	/* success */
	g_hash_table_insert(priv->auth_variables, GUINT_TO_POINTER(kind), g_object_ref(siglist));
	return g_steal_pointer(&siglist);
}

/**
 * fu_efivars_set_secure_boot_siglist:
 * @self: a #FuEfivars
 * @kind: a #FuEfiSecureBootSiglistKind, e.g. %FU_EFI_SECURE_BOOT_SIGLIST_KIND_PK
 * @firmware: (not nullable): a #FuEfiSignatureList
 * @error: (nullable): optional return location for an error
 *
 * Sets a well-known UEFI Secure Boot variable, clearing the cached contents if already parsed.
 *
 * If @firmware has a payload, e.g. it was parsed with %FU_FIRMWARE_PARSE_FLAG_CACHE_STREAM, then
 * it is written as-is so that any authentication header is preserved. Otherwise the signature
 * list is written using fu_firmware_write().
 *
 * Read-only variables such as %FU_EFI_SECURE_BOOT_SIGLIST_KIND_PK and
 * %FU_EFI_SECURE_BOOT_SIGLIST_KIND_DB_DEFAULT cannot be set, unless using dummy efivars.
 *
 * Returns: %TRUE on success
 **/
gboolean
fu_efivars_set_secure_boot_siglist(FuEfivars *self,
				   FuEfiSecureBootSiglistKind kind,
				   FuFirmware *firmware,
				   GError **error)
{
	FuEfivarsPrivate *priv = GET_PRIVATE(self);
	const gchar *guid = fu_efivars_secure_boot_siglist_kind_to_guid(kind);
	const gchar *name = fu_efivars_secure_boot_siglist_kind_to_name(kind);
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail(FU_IS_EFIVARS(self), FALSE);
	g_return_val_if_fail(kind != FU_EFI_SECURE_BOOT_SIGLIST_KIND_UNKNOWN, FALSE);
	g_return_val_if_fail(guid != NULL, FALSE);
	g_return_val_if_fail(name != NULL, FALSE);
	g_return_val_if_fail(FU_IS_FIRMWARE(firmware), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* some variables are set by the platform firmware and are read-only, but allow writing
	 * them when using dummy efivars so that the self tests can set up the PK and dbDefault */
	if (!fu_efivars_secure_boot_siglist_kind_is_rw(kind) && !FU_IS_DUMMY_EFIVARS(self)) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "%s is read-only", name);
		return FALSE;
	}

	/* use the payload if set, otherwise build the siglist */
	fw = fu_firmware_get_bytes(firmware, &error_local);
	if (fw == NULL) {
		if (!g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND)) {
			g_propagate_error(error, g_steal_pointer(&error_local));
			return FALSE;
		}
		fw = fu_firmware_write(firmware, error);
		if (fw == NULL)
			return FALSE;
	}
	if (g_bytes_get_size(fw) == 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "no data for %s-%s",
			    name,
			    guid);
		return FALSE;
	}

	/* this is no longer valid */
	g_hash_table_remove(priv->auth_variables, GUINT_TO_POINTER(kind));

	/* write entire chunk to efivarsfs */
	if (!fu_efivars_set_data_bytes(
		self,
		guid,
		name,
		fw,
		FU_EFI_VARIABLE_ATTR_APPEND_WRITE |
		    FU_EFI_VARIABLE_ATTR_TIME_BASED_AUTHENTICATED_WRITE_ACCESS |
		    FU_EFI_VARIABLE_ATTR_RUNTIME_ACCESS | FU_EFI_VARIABLE_ATTR_BOOTSERVICE_ACCESS |
		    FU_EFI_VARIABLE_ATTR_NON_VOLATILE,
		error)) {
		g_prefix_error(error,
			       "failed to write %s: ",
			       fu_efi_secure_boot_siglist_kind_to_string(kind));
		return FALSE;
	}

	/* success */
	return TRUE;
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
 * fu_efivars_space_free:
 * @self: a #FuEfivars
 * @error: (nullable): optional return location for an error
 *
 * Gets the free size available for new EFI variables, as reported from QueryVariableInfo.
 *
 * Returns: free space in bytes, or %G_MAXUINT64 on error
 **/
guint64
fu_efivars_space_free(FuEfivars *self, GError **error)
{
	FuEfivarsClass *efivars_class = FU_EFIVARS_GET_CLASS(self);

	g_return_val_if_fail(FU_IS_EFIVARS(self), G_MAXUINT64);
	g_return_val_if_fail(error == NULL || *error == NULL, G_MAXUINT64);

	if (efivars_class->space_free == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "not supported");
		return G_MAXUINT64;
	}
	return efivars_class->space_free(self, error);
}

/**
 * fu_efivars_set_data:
 * @self: a #FuEfivars
 * @guid: Globally unique identifier
 * @name: Variable name
 * @data: (not nullable): Data to set
 * @sz: size of @data
 * @attr: #FuEfiVariableAttrs, e.g. %FU_EFI_VARIABLE_ATTR_NON_VOLATILE
 * @error: (nullable): optional return location for an error
 *
 * Sets the data to a UEFI variable in NVRAM
 *
 * Returns: %TRUE on success
 **/
gboolean
fu_efivars_set_data(FuEfivars *self,
		    const gchar *guid,
		    const gchar *name,
		    const guint8 *data,
		    gsize sz,
		    FuEfiVariableAttrs attr,
		    GError **error)
{
	FuEfivarsClass *efivars_class = FU_EFIVARS_GET_CLASS(self);

	g_return_val_if_fail(FU_IS_EFIVARS(self), FALSE);
	g_return_val_if_fail(guid != NULL, FALSE);
	g_return_val_if_fail(name != NULL, FALSE);
	g_return_val_if_fail(data != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* an empty payload would delete the variable, use fu_efivars_delete() for that */
	if (sz == 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "no data to set for %s-%s",
			    name,
			    guid);
		return FALSE;
	}
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
 * @bytes: (not nullable): data blob
 * @attr: #FuEfiVariableAttrs, e.g. %FU_EFI_VARIABLE_ATTR_NON_VOLATILE
 * @error: (nullable): optional return location for an error
 *
 * Sets the data to a UEFI variable in NVRAM
 *
 * Returns: %TRUE on success
 **/
gboolean
fu_efivars_set_data_bytes(FuEfivars *self,
			  const gchar *guid,
			  const gchar *name,
			  GBytes *bytes,
			  FuEfiVariableAttrs attr,
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
 * fu_efivars_get_secure_boot:
 * @self: a #FuEfivars
 * @state: (out) (nullable): the SecureBoot state
 * @error: (nullable): optional return location for an error
 *
 * Determines if secure boot was enabled
 *
 * Returns: %TRUE on success
 **/
gboolean
fu_efivars_get_secure_boot(FuEfivars *self, FuEfiSecureBootState *state, GError **error)
{
	gsize secure_boot_size = 0;
	gsize setup_mode_size = 0;
	FuEfiSecureBootState state_tmp = FU_EFI_SECURE_BOOT_STATE_DISABLED;
	g_autofree guint8 *secure_boot = NULL;
	g_autofree guint8 *setup_mode = NULL;

	g_return_val_if_fail(FU_IS_EFIVARS(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* available, but not enabled */
	if (!fu_efivars_get_data(self,
				 FU_EFIVARS_GUID_EFI_GLOBAL,
				 "SecureBoot",
				 &secure_boot,
				 &secure_boot_size,
				 NULL,
				 NULL)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "SecureBoot is not available");
		return FALSE;
	}
	if (secure_boot_size == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "SecureBoot variable was empty");
		return FALSE;
	}
	if (secure_boot != NULL) {
		if ((secure_boot[0] & 0x01) > 0)
			state_tmp |= FU_EFI_SECURE_BOOT_STATE_ENABLED;
	}

	/* available, but not enabled */
	if (!fu_efivars_get_data(self,
				 FU_EFIVARS_GUID_EFI_GLOBAL,
				 "SetupMode",
				 &setup_mode,
				 &setup_mode_size,
				 NULL,
				 NULL)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "SetupMode is not available");
		return FALSE;
	}
	if (setup_mode_size == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "SetupMode variable was empty");
		return FALSE;
	}
	if (setup_mode != NULL) {
		if ((setup_mode[0] & 0x01) > 0)
			state_tmp |= FU_EFI_SECURE_BOOT_STATE_IN_SETUP;
	}

	/* success */
	if (state != NULL)
		*state = state_tmp;
	return TRUE;
}

/**
 * fu_efivars_set_secure_boot: (skip):
 **/
gboolean
fu_efivars_set_secure_boot(FuEfivars *self, gboolean enabled, GError **error)
{
	guint8 value = enabled ? 0x01 : 0x00;
	g_return_val_if_fail(FU_IS_EFIVARS(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	return fu_efivars_set_data(self,
				   FU_EFIVARS_GUID_EFI_GLOBAL,
				   "SecureBoot",
				   &value,
				   sizeof(value),
				   FU_EFI_VARIABLE_ATTR_BOOTSERVICE_ACCESS,
				   error);
}

/**
 * fu_efivars_get_boot_next:
 * @self: a #FuEfivars
 * @idx: (out) (nullable): boot index, typically 0x0001
 * @error: #GError
 *
 * Gets the index of the `BootNext` variable.
 *
 * Returns: %TRUE on success
 **/
gboolean
fu_efivars_get_boot_next(FuEfivars *self, guint16 *idx, GError **error)
{
	g_autofree guint8 *buf = NULL;
	gsize bufsz = 0;

	g_return_val_if_fail(FU_IS_EFIVARS(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (!fu_efivars_get_data(self,
				 FU_EFIVARS_GUID_EFI_GLOBAL,
				 "BootNext",
				 &buf,
				 &bufsz,
				 NULL,
				 error))
		return FALSE;
	if (bufsz != sizeof(guint16)) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA, "invalid size");
		return FALSE;
	}
	if (idx != NULL)
		*idx = fu_memread_uint16(buf, G_LITTLE_ENDIAN);

	/* success */
	return TRUE;
}

/**
 * fu_efivars_set_boot_next:
 * @self: a #FuEfivars
 * @idx: boot index, typically 0x0001
 * @error: #GError
 *
 * Sets the index of the `BootNext` variable.
 *
 * Returns: %TRUE on success
 **/
gboolean
fu_efivars_set_boot_next(FuEfivars *self, guint16 idx, GError **error)
{
	guint8 buf[2] = {0};
	g_return_val_if_fail(FU_IS_EFIVARS(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	fu_memwrite_uint16(buf, idx, G_LITTLE_ENDIAN);
	return fu_efivars_set_data(self,
				   FU_EFIVARS_GUID_EFI_GLOBAL,
				   "BootNext",
				   buf,
				   sizeof(buf),
				   FU_EFI_VARIABLE_ATTR_NON_VOLATILE |
				       FU_EFI_VARIABLE_ATTR_BOOTSERVICE_ACCESS |
				       FU_EFI_VARIABLE_ATTR_RUNTIME_ACCESS,
				   error);
}

/**
 * fu_efivars_get_boot_current:
 * @self: a #FuEfivars
 * @idx: (out): boot index, typically 0x0001
 * @error: #GError
 *
 * Gets the index of the `BootCurrent` variable.
 *
 * Returns: %TRUE on success
 **/
gboolean
fu_efivars_get_boot_current(FuEfivars *self, guint16 *idx, GError **error)
{
	g_autofree guint8 *buf = NULL;
	gsize bufsz = 0;

	g_return_val_if_fail(FU_IS_EFIVARS(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (!fu_efivars_get_data(self,
				 FU_EFIVARS_GUID_EFI_GLOBAL,
				 "BootCurrent",
				 &buf,
				 &bufsz,
				 NULL,
				 error))
		return FALSE;
	if (bufsz != sizeof(guint16)) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA, "invalid size");
		return FALSE;
	}
	if (idx != NULL)
		*idx = fu_memread_uint16(buf, G_LITTLE_ENDIAN);

	/* success */
	return TRUE;
}

/**
 * fu_efivars_set_boot_current: (skip):
 **/
gboolean
fu_efivars_set_boot_current(FuEfivars *self, guint16 idx, GError **error)
{
	guint8 buf[2] = {0};

	g_return_val_if_fail(FU_IS_EFIVARS(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	fu_memwrite_uint16(buf, idx, G_LITTLE_ENDIAN);
	return fu_efivars_set_data(self,
				   FU_EFIVARS_GUID_EFI_GLOBAL,
				   "BootCurrent",
				   buf,
				   sizeof(buf),
				   FU_EFI_VARIABLE_ATTR_NON_VOLATILE |
				       FU_EFI_VARIABLE_ATTR_RUNTIME_ACCESS,
				   error);
}

/**
 * fu_efivars_get_boot_order:
 * @self: a #FuEfivars
 * @error: #GError
 *
 * Gets the indexes of the `BootOrder` variable.
 *
 * Returns: (transfer full) (element-type guint16): boot order, or %NULL on error
 **/
GArray *
fu_efivars_get_boot_order(FuEfivars *self, GError **error)
{
	gsize bufsz = 0;
	g_autofree guint8 *buf = NULL;
	g_autoptr(GArray) order = g_array_new(FALSE, FALSE, sizeof(guint16));

	g_return_val_if_fail(FU_IS_EFIVARS(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	if (!fu_efivars_get_data(self,
				 FU_EFIVARS_GUID_EFI_GLOBAL,
				 "BootOrder",
				 &buf,
				 &bufsz,
				 NULL,
				 error))
		return NULL;
	if (bufsz % sizeof(guint16) != 0) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA, "invalid size");
		return NULL;
	}
	for (gsize i = 0; i < bufsz; i += sizeof(guint16)) {
		guint16 idx = fu_memread_uint16(buf + i, G_LITTLE_ENDIAN);
		g_array_append_val(order, idx);
	}

	/* success */
	return g_steal_pointer(&order);
}

/**
 * fu_efivars_set_boot_order:
 * @self: a #FuEfivars
 * @order: (element-type guint16): boot order
 * @error: #GError
 *
 * Sets the index of the `BootNext` variable.
 *
 * Returns: %TRUE on success
 **/
gboolean
fu_efivars_set_boot_order(FuEfivars *self, GArray *order, GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();

	g_return_val_if_fail(FU_IS_EFIVARS(self), FALSE);
	g_return_val_if_fail(order != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	for (guint i = 0; i < order->len; i++) {
		guint16 idx = g_array_index(order, guint16, i);
		fu_byte_array_append_uint16(buf, idx, G_LITTLE_ENDIAN);
	}
	return fu_efivars_set_data(self,
				   FU_EFIVARS_GUID_EFI_GLOBAL,
				   "BootOrder",
				   buf->data,
				   buf->len,
				   FU_EFI_VARIABLE_ATTR_NON_VOLATILE |
				       FU_EFI_VARIABLE_ATTR_BOOTSERVICE_ACCESS |
				       FU_EFI_VARIABLE_ATTR_RUNTIME_ACCESS,
				   error);
}

/**
 * fu_efivars_build_boot_order: (skip)
 **/
gboolean
fu_efivars_build_boot_order(FuEfivars *self, GError **error, ...)
{
	va_list args;
	g_autoptr(GArray) order = g_array_new(FALSE, FALSE, sizeof(guint16));

	g_return_val_if_fail(FU_IS_EFIVARS(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	va_start(args, error);
	while (TRUE) {
		guint16 idx = va_arg(args, guint);
		if (idx == G_MAXUINT16)
			break;
		g_array_append_val(order, idx);
	}
	va_end(args);

	/* success */
	return fu_efivars_set_boot_order(self, order, error);
}

/**
 * fu_efivars_create_boot_entry_for_volume:
 * @self: a #FuEfivars
 * @idx: boot index, typically 0x0001
 * @volume: a #FuVolume
 * @name: a display name, e.g. "Fedora"
 * @target: an EFI binary, e.g. "shim.efi"
 * @error: #GError
 *
 * Creates a BootXXXX variable for a given volume, name and target.
 *
 * If @target does not exist on the volume then a dummy file is created.
 *
 * Returns: %TRUE on success
 **/
gboolean
fu_efivars_create_boot_entry_for_volume(FuEfivars *self,
					guint16 idx,
					FuVolume *volume,
					const gchar *name,
					const gchar *target,
					GError **error)
{
	g_autoptr(FuEfiDevicePathList) devpath_list = fu_efi_device_path_list_new();
	g_autoptr(FuEfiFilePathDevicePath) dp_fp = NULL;
	g_autoptr(FuEfiHardDriveDevicePath) dp_hdd = NULL;
	g_autoptr(FuEfiLoadOption) entry = fu_efi_load_option_new();
	g_autoptr(GFile) file = NULL;
	g_autofree gchar *mount_point = NULL;

	g_return_val_if_fail(FU_IS_EFIVARS(self), FALSE);
	g_return_val_if_fail(FU_IS_VOLUME(volume), FALSE);
	g_return_val_if_fail(name != NULL, FALSE);
	g_return_val_if_fail(target != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* create plausible EFI file if not already exists */
	mount_point = fu_volume_get_mount_point(volume);
	if (mount_point == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "volume has no mount point");
		return FALSE;
	}
	file = g_file_new_build_filename(mount_point, target, NULL);
	if (!g_file_query_exists(file, NULL)) {
		g_autoptr(FuFirmware) img_text = fu_firmware_new();
		g_autoptr(FuFirmware) pefile = fu_pefile_firmware_new();
		g_autoptr(GBytes) img_blob = g_bytes_new_static("hello", 5);
		fu_firmware_set_id(img_text, ".text");
		fu_firmware_set_bytes(img_text, img_blob);
		if (!fu_firmware_add_image(pefile, img_text, error))
			return FALSE;
		if (!fu_firmware_write_file(pefile, file, error))
			return FALSE;
	}

	dp_hdd = fu_efi_hard_drive_device_path_new_from_volume(volume, error);
	if (dp_hdd == NULL)
		return FALSE;
	dp_fp = fu_efi_file_path_device_path_new();
	if (!fu_efi_file_path_device_path_set_name(dp_fp, target, error))
		return FALSE;
	if (!fu_firmware_add_image(FU_FIRMWARE(devpath_list), FU_FIRMWARE(dp_hdd), error))
		return FALSE;
	if (!fu_firmware_add_image(FU_FIRMWARE(devpath_list), FU_FIRMWARE(dp_fp), error))
		return FALSE;

	fu_firmware_set_id(FU_FIRMWARE(entry), name);
	if (!fu_firmware_add_image(FU_FIRMWARE(entry), FU_FIRMWARE(devpath_list), error))
		return FALSE;
	return fu_efivars_set_boot_entry(self, idx, entry, error);
}

/**
 * fu_efivars_get_boot_data:
 * @self: a #FuEfivars
 * @idx: boot index, typically 0x0001
 * @error: #GError
 *
 * Gets the raw data of the `BootXXXX` variable.
 *
 * Returns: (transfer full): boot data
 **/
GBytes *
fu_efivars_get_boot_data(FuEfivars *self, guint16 idx, GError **error)
{
	g_autofree gchar *name = g_strdup_printf("Boot%04X", idx);
	g_return_val_if_fail(FU_IS_EFIVARS(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);
	return fu_efivars_get_data_bytes(self, FU_EFIVARS_GUID_EFI_GLOBAL, name, NULL, error);
}

/**
 * fu_efivars_set_boot_data:
 * @self: a #FuEfivars
 * @idx: boot index, typically 0x0001
 * @blob: #GBytes
 * @error: #GError
 *
 * Sets the raw data of the `BootXXXX` variable. If @blob is %NULL then the boot entry is deleted.
 *
 * Returns: %TRUE for success
 **/
gboolean
fu_efivars_set_boot_data(FuEfivars *self, guint16 idx, GBytes *blob, GError **error)
{
	g_autofree gchar *name = g_strdup_printf("Boot%04X", idx);
	g_return_val_if_fail(FU_IS_EFIVARS(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (blob == NULL)
		return fu_efivars_delete(self, FU_EFIVARS_GUID_EFI_GLOBAL, name, error);
	return fu_efivars_set_data_bytes(self,
					 FU_EFIVARS_GUID_EFI_GLOBAL,
					 name,
					 blob,
					 FU_EFI_VARIABLE_ATTR_NON_VOLATILE |
					     FU_EFI_VARIABLE_ATTR_BOOTSERVICE_ACCESS |
					     FU_EFI_VARIABLE_ATTR_RUNTIME_ACCESS,
					 error);
}

/**
 * fu_efivars_get_boot_entry:
 * @self: a #FuEfivars
 * @idx: boot index, typically 0x0001
 * @error: #GError
 *
 * Gets the loadopt data of the `BootXXXX` variable.
 *
 * Returns: (transfer full): a #FuEfiLoadOption, or %NULL
 **/
FuEfiLoadOption *
fu_efivars_get_boot_entry(FuEfivars *self, guint16 idx, GError **error)
{
	g_autofree gchar *name = g_strdup_printf("Boot%04X", idx);
	g_autoptr(FuEfiLoadOption) loadopt = fu_efi_load_option_new();
	g_autoptr(GBytes) blob = NULL;

	g_return_val_if_fail(FU_IS_EFIVARS(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* get data */
	blob = fu_efivars_get_data_bytes(self, FU_EFIVARS_GUID_EFI_GLOBAL, name, NULL, error);
	if (blob == NULL)
		return NULL;
	if (!fu_firmware_parse_bytes(FU_FIRMWARE(loadopt),
				     blob,
				     0x0,
				     FU_FIRMWARE_PARSE_FLAG_NONE,
				     error))
		return NULL;
	fu_firmware_set_idx(FU_FIRMWARE(loadopt), idx);
	return g_steal_pointer(&loadopt);
}

/**
 * fu_efivars_set_boot_entry:
 * @self: a #FuEfivars
 * @idx: boot index, typically 0x0001
 * @entry: a #FuEfiLoadOption
 * @error: #GError
 *
 * Sets the loadopt data of the `BootXXXX` variable.
 *
 * Returns: %TRUE for success
 **/
gboolean
fu_efivars_set_boot_entry(FuEfivars *self, guint16 idx, FuEfiLoadOption *entry, GError **error)
{
	g_autoptr(GBytes) blob = NULL;

	g_return_val_if_fail(FU_IS_EFIVARS(self), FALSE);
	g_return_val_if_fail(FU_IS_EFI_LOAD_OPTION(entry), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	blob = fu_firmware_write(FU_FIRMWARE(entry), error);
	if (blob == NULL)
		return FALSE;
	return fu_efivars_set_boot_data(self, idx, blob, error);
}

/**
 * fu_efivars_get_boot_entries:
 * @self: a #FuEfivars
 * @error: #GError
 *
 * Gets the loadopt data for all the entries listed in `BootOrder`.
 *
 * Returns: (transfer full) (element-type FuEfiLoadOption): boot data
 **/
GPtrArray *
fu_efivars_get_boot_entries(FuEfivars *self, GError **error)
{
	g_autoptr(GArray) order = NULL;
	g_autoptr(GPtrArray) array = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);

	g_return_val_if_fail(FU_IS_EFIVARS(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	order = fu_efivars_get_boot_order(self, error);
	if (order == NULL)
		return NULL;
	for (guint i = 0; i < order->len; i++) {
		guint16 idx = g_array_index(order, guint16, i);
		g_autoptr(FuEfiLoadOption) loadopt = NULL;

		loadopt = fu_efivars_get_boot_entry(self, idx, error);
		if (loadopt == NULL) {
			g_prefix_error(error, "failed to load Boot%04X: ", idx);
			return NULL;
		}
		g_ptr_array_add(array, g_steal_pointer(&loadopt));
	}

	/* success */
	return g_steal_pointer(&array);
}

static void
fu_efivars_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	FuEfivars *self = FU_EFIVARS(object);
	FuEfivarsPrivate *priv = GET_PRIVATE(self);
	switch (prop_id) {
	case PROP_PATH_STORE:
		g_value_set_object(value, priv->pstore);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fu_efivars_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	FuEfivars *self = FU_EFIVARS(object);
	FuEfivarsPrivate *priv = GET_PRIVATE(self);
	switch (prop_id) {
	case PROP_PATH_STORE:
		g_set_object(&priv->pstore, g_value_get_object(value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fu_efivars_init(FuEfivars *self)
{
	FuEfivarsPrivate *priv = GET_PRIVATE(self);
	priv->auth_variables = g_hash_table_new_full(g_direct_hash,
						     g_direct_equal,
						     NULL,
						     (GDestroyNotify)g_object_unref);
}

static void
fu_efivars_finalize(GObject *object)
{
	FuEfivars *self = FU_EFIVARS(object);
	FuEfivarsPrivate *priv = GET_PRIVATE(self);

	if (priv->pstore != NULL)
		g_object_unref(priv->pstore);
	g_hash_table_unref(priv->auth_variables);

	G_OBJECT_CLASS(fu_efivars_parent_class)->finalize(object);
}

static void
fu_efivars_class_init(FuEfivarsClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	GParamSpec *pspec;

	object_class->get_property = fu_efivars_get_property;
	object_class->set_property = fu_efivars_set_property;
	object_class->finalize = fu_efivars_finalize;

	pspec = g_param_spec_object("path-store",
				    NULL,
				    NULL,
				    FU_TYPE_PATH_STORE,
				    G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_PATH_STORE, pspec);
}
