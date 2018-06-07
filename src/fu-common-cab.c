/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <libgcab.h>

#include "fu-common-cab.h"
#include "fu-common.h"

#include "fwupd-error.h"

#ifdef HAVE_GCAB_1_0

static GCabFile *
_gcab_cabinet_get_file_by_name (GCabCabinet *cabinet, const gchar *basename)
{
	GPtrArray *folders = gcab_cabinet_get_folders (cabinet);
	for (guint i = 0; i < folders->len; i++) {
		GCabFolder *cabfolder = GCAB_FOLDER (g_ptr_array_index (folders, i));
		GCabFile *cabfile = gcab_folder_get_file_by_name (cabfolder, basename);
		if (cabfile != NULL)
			return cabfile;
	}
	return NULL;
}

/* sets the firmware and signature blobs on AsRelease */
static gboolean
fu_common_store_from_cab_release (AsRelease *release, GCabCabinet *cabinet, GError **error)
{
	AsChecksum *csum_tmp;
	GCabFile *cabfile;
	GBytes *blob;
	guint64 size;
	g_autofree gchar *basename = NULL;
	g_autofree gchar *checksum = NULL;
	const gchar *suffixes[] = { "asc", "p7b", "p7c", NULL };

	/* ensure we always have a content checksum */
	csum_tmp = as_release_get_checksum_by_target (release, AS_CHECKSUM_TARGET_CONTENT);
	if (csum_tmp == NULL) {
		g_autoptr(AsChecksum) csum = as_checksum_new ();
		as_checksum_set_target (csum, AS_CHECKSUM_TARGET_CONTENT);
		/* if this isn't true, a firmware needs to set in
		 * the metainfo.xml file something like:
		 * <checksum target="content" filename="FLASH.ROM"/> */
		as_checksum_set_filename (csum, "firmware.bin");
		as_release_add_checksum (release, csum);
		csum_tmp = csum;
	}

	/* get the main firmware file */
	basename = g_path_get_basename (as_checksum_get_filename (csum_tmp));
	cabfile = _gcab_cabinet_get_file_by_name (cabinet, basename);
	if (cabfile == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "cannot find %s in archive",
			     as_checksum_get_filename (csum_tmp));
		return FALSE;
	}
	blob = gcab_file_get_bytes (cabfile);
	if (blob == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "no GBytes from GCabFile firmware");
		return FALSE;
	}

	/* set the blob */
	as_release_set_blob (release, basename, blob);

	/* set if unspecified, but error out if specified and incorrect */
	size = as_release_get_size (release, AS_SIZE_KIND_INSTALLED);
	if (size == 0) {
		as_release_set_size (release, AS_SIZE_KIND_INSTALLED,
				     g_bytes_get_size (blob));
	} else if (size != g_bytes_get_size (blob)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "contents size invalid, expected "
			     "%" G_GSIZE_FORMAT ", got %" G_GUINT64_FORMAT,
			     g_bytes_get_size (blob), size);
		return FALSE;
	}

	/* set if unspecified, but error out if specified and incorrect */
	checksum = g_compute_checksum_for_bytes (G_CHECKSUM_SHA1, blob);
	if (as_checksum_get_value (csum_tmp) == NULL) {
		as_checksum_set_value (csum_tmp, checksum);
	} else if (g_strcmp0 (checksum, as_checksum_get_value (csum_tmp)) != 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "contents checksum invalid, expected %s, got %s",
			     checksum,
			     as_checksum_get_value (csum_tmp));
		return FALSE;
	}

	/* if the signing file exists, set that too */
	for (guint i = 0; suffixes[i] != NULL; i++) {
		g_autofree gchar *basename_sig = NULL;
		basename_sig = g_strdup_printf ("%s.%s", basename, suffixes[i]);
		cabfile = _gcab_cabinet_get_file_by_name (cabinet, basename_sig);
		if (cabfile != NULL) {
			blob = gcab_file_get_bytes (cabfile);
			if (blob == NULL) {
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INVALID_FILE,
					     "no GBytes from GCabFile %s",
					     basename_sig);
				return FALSE;
			}
			as_release_set_blob (release, basename_sig, blob);
		}
	}

	/* success */
	return TRUE;
}

/* adds each GCabFile to the store */
static gboolean
fu_common_store_from_cab_file (AsStore *store, GCabCabinet *cabinet,
			       GCabFile *cabfile, GError **error)
{
	GBytes *blob;
	GPtrArray *releases;
	g_autoptr(AsApp) app = NULL;
	g_autoptr(GError) error_local = NULL;
#if !AS_CHECK_VERSION(0,7,5)
	g_autofree gchar *cache_fn = NULL;
	g_autofree gchar *cachedir = NULL;
#endif

	/* parse file */
	blob = gcab_file_get_bytes (cabfile);
	if (blob == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "no GBytes from GCabFile");
		return FALSE;
	}
	app = as_app_new ();
#if AS_CHECK_VERSION(0,7,5)
	if (!as_app_parse_data (app, blob, AS_APP_PARSE_FLAG_NONE, &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "could not parse MetaInfo XML: %s",
			     error_local->message);
		return FALSE;
	}
#else
	cachedir = fu_common_get_path (FU_PATH_KIND_CACHEDIR_PKG);
	cache_fn = g_build_filename (cachedir, gcab_file_get_extract_name (cabfile), NULL);
	if (!fu_common_mkdir_parent (cache_fn, error))
		return FALSE;
	if (!g_file_set_contents (cache_fn, g_bytes_get_data (blob, NULL),
				  g_bytes_get_size (blob), &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_WRITE,
			     "could not save temporary MetaInfo XML to %s: %s",
			     cache_fn, error_local->message);
		return FALSE;
	}
	if (!as_app_parse_file (app, cache_fn, AS_APP_PARSE_FLAG_NONE, &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "could not parse MetaInfo XML: %s",
			     error_local->message);
		return FALSE;
	}
#endif

	/* process each listed release */
	releases = as_app_get_releases (app);
	if (releases->len == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "no releases in metainfo file");
		return FALSE;
	}
	for (guint i = 0; i < releases->len; i++) {
		AsRelease *release = g_ptr_array_index (releases, i);
		g_debug ("processing release: %s", as_release_get_version (release));
		if (!fu_common_store_from_cab_release (release, cabinet, error))
			return FALSE;
	}

	/* success */
	as_store_add_app (store, app);
	return TRUE;
}

/* adds each GCabFolder to the store */
static gboolean
fu_common_store_from_cab_folder (AsStore *store, GCabCabinet *cabinet,
				 GCabFolder *cabfolder, GError **error)
{
	g_autoptr(GSList) cabfiles = gcab_folder_get_files (cabfolder);
	for (GSList *l = cabfiles; l != NULL; l = l->next) {
		GCabFile *cabfile = GCAB_FILE (l->data);
		const gchar *fn = gcab_file_get_extract_name (cabfile);
		g_debug ("processing file: %s", fn);
		if (as_format_guess_kind (fn) == AS_FORMAT_KIND_METAINFO) {
			if (!fu_common_store_from_cab_file (store, cabinet, cabfile, error)) {
				g_prefix_error (error, "%s could not be loaded: ",
						gcab_file_get_extract_name (cabfile));
				return FALSE;
			}
		}
	}
	return TRUE;
}

typedef struct {
	guint64		 size_total;
	guint64		 size_max;
	GError		*error;
} FuCommonCabHelper;

static gboolean
as_cab_store_file_cb (GCabFile *file, gpointer user_data)
{
	FuCommonCabHelper *helper = (FuCommonCabHelper *) user_data;
	g_autofree gchar *basename = NULL;
	g_autofree gchar *name = NULL;

	/* already failed */
	if (helper->error != NULL)
		return FALSE;

	/* check the size of the compressed file */
	if (gcab_file_get_size (file) > helper->size_max) {
		g_autofree gchar *sz_val = g_format_size (gcab_file_get_size (file));
		g_autofree gchar *sz_max = g_format_size (helper->size_max);
		g_set_error (&helper->error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "file %s was too large (%s, limit %s)",
			     gcab_file_get_name (file),
			     sz_val, sz_max);
		return FALSE;
	}

	/* check the total size of all the compressed files */
	helper->size_total += gcab_file_get_size (file);
	if (helper->size_total > helper->size_max) {
		g_autofree gchar *sz_val = g_format_size (helper->size_total);
		g_autofree gchar *sz_max = g_format_size (helper->size_max);
		g_set_error (&helper->error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "uncompressed data too large (%s, limit %s)",
			     sz_val, sz_max);
		return FALSE;
	}

	/* convert to UNIX paths */
	name = g_strdup (gcab_file_get_name (file));
	g_strdelimit (name, "\\", '/');

	/* ignore the dirname completely */
	basename = g_path_get_basename (name);
	gcab_file_set_extract_name (file, basename);
	return TRUE;
}

/**
 * fu_common_store_from_cab_bytes:
 * @blob: A readable blob
 * @size_max: The maximum size of the archive
 * @error: A #FuEndianType, e.g. %G_LITTLE_ENDIAN
 *
 * Create an AppStream store from a cabinet archive.
 *
 * Returns: a store, or %NULL on error
 **/
AsStore *
fu_common_store_from_cab_bytes (GBytes *blob, guint64 size_max, GError **error)
{
	FuCommonCabHelper helper = { 0 };
	GPtrArray *folders;
	g_autoptr(AsStore) store = as_store_new ();
	g_autoptr(GCabCabinet) cabinet = gcab_cabinet_new ();
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GInputStream) ip = NULL;

	/* load from a seekable stream */
	ip = g_memory_input_stream_new_from_bytes (blob);
	if (!gcab_cabinet_load (cabinet, ip, NULL, error))
		return NULL;

	/* check the size is sane */
	if (gcab_cabinet_get_size (cabinet) > size_max) {
		g_autofree gchar *sz_val = g_format_size (gcab_cabinet_get_size (cabinet));
		g_autofree gchar *sz_max = g_format_size (size_max);
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "archive too large (%s, limit %s)",
			     sz_val, sz_max);
		return NULL;
	}

	/* decompress the file to memory */
	helper.size_max = size_max;
	if (!gcab_cabinet_extract_simple (cabinet, NULL,
					  as_cab_store_file_cb, &helper,
					  NULL, &error_local)) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     error_local->message);
		return NULL;
	}

	/* the file callback set an error */
	if (helper.error != NULL) {
		g_propagate_error (error, helper.error);
		return NULL;
	}

	/* look at each folder */
	folders = gcab_cabinet_get_folders (cabinet);
	for (guint i = 0; i < folders->len; i++) {
		GCabFolder *cabfolder = GCAB_FOLDER (g_ptr_array_index (folders, i));
		g_debug ("processing folder: %u/%u", i + 1, folders->len);
		if (!fu_common_store_from_cab_folder (store, cabinet, cabfolder, error))
			return NULL;
	}

	/* did we get any valid AsApps */
	if (as_store_get_size (store) == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "archive contained no valid metadata");
		return NULL;
	}

	/* success */
	return g_steal_pointer (&store);
}

#else

AsStore *
fu_common_store_from_cab_bytes (GBytes *blob, guint64 size_max, GError **error)
{
	g_autoptr(AsStore) store = as_store_new ();
	g_autoptr(GError) error_local = NULL;

	/* this is klunky as we have to write actual files to /tmp */
	if (!as_store_from_bytes (store, blob, NULL, &error_local)) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     error_local->message);
		return NULL;
	}

	/* did we get any valid AsApps */
	if (as_store_get_size (store) == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "archive contained no valid metadata");
		return NULL;
	}
	return g_steal_pointer (&store);
}
#endif
