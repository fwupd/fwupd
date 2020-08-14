/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-common.h"
#include "fu-efi-signature-common.h"
#include "fu-volume.h"

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

static gchar *
fu_uefi_dbx_get_authenticode_hash_pesign (const gchar *fn, GError **error)
{
	const gchar *argv[] = { "pesign", "-P", "-i", fn, "-h", NULL };
	gint exit_status = -1;
	g_autofree gchar *standard_output = NULL;
	g_autofree gchar *standard_error = NULL;
	g_auto(GStrv) sections = NULL;

	/* get the Authenticode hash: ideally we'd be using libpesign but that
	 * does not exist yet... */
	if (!g_spawn_sync (NULL, (gchar **) argv, NULL, G_SPAWN_SEARCH_PATH,
			   NULL, NULL, &standard_output, &standard_error,
			   &exit_status, error)) {
		g_prefix_error (error, "failed to exec pesign: ");
		return NULL;
	}
	if (exit_status != 0) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_ARGUMENT,
			     "failed to exec pesign rc:%i: %s",
			     exit_status, standard_error);
		return NULL;
	}
	sections = g_strsplit (standard_output, " ", 2);
	if (g_strv_length (sections) != 2) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "failed to parse '%s'",
			     standard_output);
		return NULL;
	}
	g_strdelimit (sections[1], "\n\r", '\0');
	return g_strdup (sections[1]);
}

static gchar *
fu_uefi_dbx_get_authenticode_hash_sbsign (const gchar *fn, GError **error)
{
	const gchar *argv[] = { "sbsign", "--hash-only", fn, NULL };
	gint exit_status = -1;
	g_autofree gchar *standard_output = NULL;
	g_autofree gchar *standard_error = NULL;

	/* get the Authenticode hash: ideally we'd be using libpesign but that
	 * does not exist yet... */
	if (!g_spawn_sync (NULL, (gchar **) argv, NULL, G_SPAWN_SEARCH_PATH,
			   NULL, NULL, &standard_output, &standard_error,
			   &exit_status, error)) {
		g_prefix_error (error, "failed to exec pesign: ");
		return NULL;
	}
	if (exit_status != 0) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_ARGUMENT,
			     "failed to exec pesign rc:%i: %s",
			     exit_status, standard_error);
		return NULL;
	}
	g_strdelimit (standard_output, "\n\r", '\0');
	return g_steal_pointer (&standard_output);
}

gchar *
fu_uefi_dbx_get_authenticode_hash (const gchar *fn, GError **error)
{
	const gchar *content_type;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GFileInfo) info = NULL;

	/* check is a EFI binary */
	file = g_file_new_for_path (fn);
	info = g_file_query_info (file,
				  G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE,
				  G_FILE_QUERY_INFO_NONE,
				  NULL,
				  error);
	if (info == NULL)
		return NULL;
	content_type = g_file_info_get_attribute_string (info, G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE);
	if (g_strcmp0 (content_type, "application/x-ms-dos-executable") != 0) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "invalid content type of %s",
			     content_type);
		return NULL;
	}

	/* pesign */
	if (g_file_test ("/usr/bin/pesign", G_FILE_TEST_EXISTS))
		return fu_uefi_dbx_get_authenticode_hash_pesign (fn, error);
	if (g_file_test ("/usr/bin/sbsign", G_FILE_TEST_EXISTS))
		return fu_uefi_dbx_get_authenticode_hash_sbsign (fn, error);

	/* neither file exists */
	g_set_error (error,
		     G_IO_ERROR,
		     G_IO_ERROR_NOT_FOUND,
		     "could not find pesign or sbsign");
	return NULL;
}

static gboolean
fu_uefi_dbx_signature_list_validate_volume (GPtrArray *siglists, FuVolume *esp, GError **error)
{
	g_autofree gchar *esp_path = NULL;
	g_autoptr(GPtrArray) files = NULL;

	/* get list of files contained in the ESP */
	esp_path = fu_volume_get_mount_point (esp);
	if (esp_path == NULL)
		return TRUE;
	files = fu_common_get_files_recursive (esp_path, error);
	if (files == NULL)
		return FALSE;

	/* verify each file does not exist in the ESP */
	for (guint i = 0; i < files->len; i++) {
		const gchar *fn = g_ptr_array_index (files, i);
		g_autofree gchar *checksum = NULL;
		g_autoptr(GError) error_local = NULL;

		/* get checksum of file */
		checksum = fu_uefi_dbx_get_authenticode_hash (fn, &error_local);
		if (checksum == NULL) {
			g_debug ("failed to get checksum for %s: %s", fn, error_local->message);
			continue;
		}

		/* Authenticode signature is present in dbx! */
		g_debug ("fn=%s, checksum=%s", fn, checksum);
		if (fu_efi_signature_list_array_has_checksum (siglists, checksum)) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "%s Authenticode checksum [%s] is present in dbx",
				     fn, checksum);
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

gboolean
fu_uefi_dbx_signature_list_validate (GPtrArray *siglists, GError **error)
{
	g_autoptr(GPtrArray) volumes = NULL;
	volumes = fu_common_get_volumes_by_kind (FU_VOLUME_KIND_ESP, error);
	if (volumes == NULL)
		return FALSE;
	for (guint i = 0; i < volumes->len; i++) {
		FuVolume *esp = g_ptr_array_index (volumes, i);
		g_autoptr(FuDeviceLocker) locker = NULL;
		locker = fu_volume_locker (esp, error);
		if (locker == NULL)
			return FALSE;
		if (!fu_uefi_dbx_signature_list_validate_volume (siglists, esp, error))
			return FALSE;
	}
	return TRUE;
}
