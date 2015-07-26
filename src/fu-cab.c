/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include <fwupd.h>
#include <appstream-glib.h>
#include <glib-object.h>
#include <gio/gio.h>
#include <libgcab.h>
#include <glib/gstdio.h>
#include <gio/gunixinputstream.h>

#include "fu-cleanup.h"
#include "fu-cab.h"
#include "fu-keyring.h"

static void fu_cab_finalize			 (GObject *object);

#define FU_CAB_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), FU_TYPE_CAB, FuCabPrivate))

/**
 * FuCabPrivate:
 *
 * Private #FuCab data
 **/
struct _FuCabPrivate
{
	GCabCabinet			*gcab;
	GInputStream			*cab_stream;
	GKeyFile			*inf_kf;
	FwupdTrustFlags			 trust_flags;
	gchar				*firmware_basename;
	gchar				*firmware_filename;
	gchar				*signature_basename;
	gchar				*cat_basename;
	gchar				*description;
	gchar				*guid;
	gchar				*inf_basename;
	gchar				*metainfo_basename;
	gchar				*tmp_path;
	gchar				*license;
	gchar				*name;
	gchar				*summary;
	gchar				*url_homepage;
	gchar				*vendor;
	gchar				*version;
	guint64				 size;
	GPtrArray			*basenames_to_delete;
	GPtrArray			*filelist;	/* with full path */
};

G_DEFINE_TYPE (FuCab, fu_cab, G_TYPE_OBJECT)

typedef struct {
	FuCab			*cab;
	FuCabExtractFlags	 flags;
} FuCabExtractHelper;

/**
 * fu_cab_add_file:
 **/
void
fu_cab_add_file (FuCab *cab, const gchar *filename)
{
	FuCabPrivate *priv = cab->priv;
	const gchar *tmp;
	guint i;
	_cleanup_free_ gchar *basename = NULL;

	/* check the same basename does not already exist */
	basename = g_path_get_basename (filename);
	for (i = 0; i < priv->filelist->len; i++) {
		_cleanup_free_ gchar *basename_tmp = NULL;
		tmp = g_ptr_array_index (priv->filelist, i);
		basename_tmp = g_path_get_basename (tmp);
		if (g_strcmp0 (basename_tmp, basename) == 0) {
			g_debug ("%s already exists, removing old", basename_tmp);
			g_ptr_array_remove_index (priv->filelist, i);
			break;
		}
	}

	/* add the full filename */
	g_ptr_array_add (cab->priv->filelist, g_strdup (filename));
}

/**
 * fu_cab_read_file_list_cb:
 **/
static gboolean
fu_cab_read_file_list_cb (GCabFile *file, gpointer user_data)
{
	FuCab *cab = FU_CAB (user_data);
	g_ptr_array_add (cab->priv->filelist,
			 g_build_filename (cab->priv->tmp_path,
					   gcab_file_get_name (file),
					   NULL));
	return FALSE;
}

/**
 * fu_cab_match_basename_flag:
 **/
static FuCabExtractFlags
fu_cab_match_basename_flag (FuCab *cab, const gchar *basename)
{
	FuCabPrivate *priv = cab->priv;
	if (g_strcmp0 (basename, priv->firmware_basename) == 0)
		return FU_CAB_EXTRACT_FLAG_FIRMWARE;
	if (g_strcmp0 (basename, priv->signature_basename) == 0)
		return FU_CAB_EXTRACT_FLAG_SIGNATURE;
	if (g_strcmp0 (basename, priv->inf_basename) == 0)
		return FU_CAB_EXTRACT_FLAG_INF;
	if (g_strcmp0 (basename, priv->metainfo_basename) == 0)
		return FU_CAB_EXTRACT_FLAG_METAINFO;
	if (g_strcmp0 (basename, priv->cat_basename) == 0)
		return FU_CAB_EXTRACT_FLAG_CATALOG;
	return FU_CAB_EXTRACT_FLAG_UNKNOWN;
}

/**
 * fu_cab_extract_cb:
 **/
static gboolean
fu_cab_extract_cb (GCabFile *file, gpointer user_data)
{
	FuCabExtractHelper *helper = (FuCabExtractHelper *) user_data;
	FuCab *cab = FU_CAB (helper->cab);
	FuCabPrivate *priv = cab->priv;
	FuCabExtractFlags match_flags;

	/* only if it matches the mask */
	match_flags = fu_cab_match_basename_flag (cab, gcab_file_get_name (file));
	if ((helper->flags & match_flags) > 0) {
		g_ptr_array_add (priv->basenames_to_delete,
				 g_strdup (gcab_file_get_name (file)));
		return TRUE;
	}

	return helper->flags == FU_CAB_EXTRACT_FLAG_ALL;
}

/**
 * fu_cab_parse:
 **/
static gboolean
fu_cab_parse (FuCab *cab, GError **error)
{
	AsRelease *rel;
	FuCabPrivate *priv = cab->priv;
	GString *update_description;
	const gchar *tmp;
	guint i;
	const gchar *fn;
	_cleanup_error_free_ GError *error_local = NULL;
	_cleanup_free_ gchar *inf_filename = NULL;
	_cleanup_object_unref_ AsApp *app = NULL;
	_cleanup_object_unref_ GFile *path = NULL;

	g_return_val_if_fail (FU_IS_CAB (cab), FALSE);

	/* open the file */
	priv->gcab = gcab_cabinet_new ();
	if (!gcab_cabinet_load (priv->gcab, priv->cab_stream, NULL, &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_READ,
			     "cannot load .cab file: %s",
			     error_local->message);
		return FALSE;
	}

	/* decompress to /tmp */
	priv->tmp_path = g_dir_make_tmp ("fwupd-XXXXXX", &error_local);
	if (priv->tmp_path == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_WRITE,
			     "failed to create temp dir: %s",
			     error_local->message);
		return FALSE;
	}

	/* get the file list */
	path = g_file_new_for_path (priv->tmp_path);
	if (!gcab_cabinet_extract_simple (priv->gcab, path,
					  fu_cab_read_file_list_cb,
					  cab, NULL, &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_WRITE,
			     "failed to extract .cab file: %s",
			     error_local->message);
		return FALSE;
	}

	/* find the .inf file in the file list */
	for (i = 0; i < priv->filelist->len; i++) {
		fn = g_ptr_array_index (priv->filelist, i);
		if (g_str_has_suffix (fn, ".inf")) {
			priv->inf_basename = g_path_get_basename (fn);
			continue;
		}
		if (g_str_has_suffix (fn, ".metainfo.xml")) {
			priv->metainfo_basename = g_path_get_basename (fn);
			continue;
		}
	}

	/* read .inf file */
	if (priv->inf_basename == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "no .inf file in.cab file");
		return FALSE;
	}

	/* extract these */
	if (!fu_cab_extract (cab, FU_CAB_EXTRACT_FLAG_INF |
				  FU_CAB_EXTRACT_FLAG_METAINFO, error))
		return FALSE;

	/* parse it */
	app = as_app_new ();
	inf_filename = g_build_filename (priv->tmp_path, priv->inf_basename, NULL);
	if (!as_app_parse_file (app, inf_filename,
				AS_APP_PARSE_FLAG_NONE, &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "%s could not be loaded: %s",
			     inf_filename, error_local->message);
		return FALSE;
	}

	/* merge with the metainfo file */
	if (priv->metainfo_basename != NULL) {
		_cleanup_free_ gchar *metainfo_filename = NULL;
		_cleanup_object_unref_ AsApp *app2 = NULL;
		app2 = as_app_new ();
		metainfo_filename = g_build_filename (priv->tmp_path,
						      priv->metainfo_basename, NULL);
		if (!as_app_parse_file (app2, metainfo_filename,
					AS_APP_PARSE_FLAG_NONE, &error_local)) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "%s could not be loaded: %s",
				     metainfo_filename,
				     error_local->message);
			return FALSE;
		}
		as_app_subsume_full (app, app2, AS_APP_SUBSUME_FLAG_NONE);
	}

	/* extract info */
	update_description = g_string_new ("");
	priv->guid = g_strdup (as_app_get_id (app));
	priv->vendor = g_strdup (as_app_get_developer_name (app, NULL));
	priv->name = g_strdup (as_app_get_name (app, NULL));
	priv->summary = g_strdup (as_app_get_comment (app, NULL));
	tmp = as_app_get_description (app, NULL);
	if (tmp != NULL)
		g_string_append (update_description, tmp);
	priv->url_homepage = g_strdup (as_app_get_url_item (app, AS_URL_KIND_HOMEPAGE));
	priv->license = g_strdup (as_app_get_project_license (app));
	rel = as_app_get_release_default (app);
	priv->version = g_strdup (as_release_get_version (rel));
	tmp = as_release_get_description (rel, NULL);
	if (tmp != NULL)
		g_string_append (update_description, tmp);
	priv->description = g_string_free (update_description, FALSE);

	/* optional */
	tmp = as_app_get_metadata_item (app, "CatalogBasename");
	if (tmp != NULL)
		priv->cat_basename = g_strdup (tmp);

	/* find out what firmware file we have to open */
	tmp = as_app_get_metadata_item (app, "FirmwareBasename");
	priv->firmware_basename = g_strdup (tmp);
	priv->firmware_filename = g_build_filename (priv->tmp_path, tmp, NULL);
	priv->signature_basename = g_strdup_printf ("%s.asc", tmp);

	/* success */
	return TRUE;
}

/**
 * fu_cab_load_fd:
 **/
gboolean
fu_cab_load_fd (FuCab *cab, gint fd, GCancellable *cancellable, GError **error)
{
	FuCabPrivate *priv = cab->priv;
	_cleanup_error_free_ GError *error_local = NULL;
	_cleanup_object_unref_ GInputStream *stream = NULL;

	g_return_val_if_fail (FU_IS_CAB (cab), FALSE);

	/* we can't get the size of the files in the .cab file, so just return
	 * the size of the cab file itself, on the logic that the firmware will
	 * be the largest thing by far, and typically be uncompressable */
	priv->size = 0;

	/* GCab needs a GSeekable input stream, so buffer to RAM then load */
	stream = g_unix_input_stream_new (fd, TRUE);
	priv->cab_stream = g_memory_input_stream_new ();
	while (1) {
		_cleanup_bytes_unref_ GBytes *data = NULL;
		data = g_input_stream_read_bytes (stream, 8192,
						  cancellable,
						  &error_local);
		if (g_bytes_get_size (data) == 0)
			break;
		if (data == NULL) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_READ,
					     error_local->message);
			return FALSE;
		}
		priv->size += g_bytes_get_size (data);
		g_memory_input_stream_add_bytes (G_MEMORY_INPUT_STREAM (priv->cab_stream), data);
	}

	/* parse */
	return fu_cab_parse (cab, error);
}

/**
 * fu_cab_save_file:
 **/
gboolean
fu_cab_save_file (FuCab *cab, GFile *file, GCancellable *cancellable, GError **error)
{
	FuCabPrivate *priv = cab->priv;
	const gchar *tmp;
	guint i;
	_cleanup_object_unref_ GCabCabinet *gcab = NULL;
	_cleanup_object_unref_ GCabFolder *folder = NULL;
	_cleanup_object_unref_ GOutputStream *stream = NULL;

	g_return_val_if_fail (FU_IS_CAB (cab), FALSE);

	/* ensure all files are decompressed */
	if (!fu_cab_extract (cab, FU_CAB_EXTRACT_FLAG_ALL, error))
		return FALSE;

	/* create a new archive, we can't reuse the existing instance */
	gcab = gcab_cabinet_new ();
	folder = gcab_folder_new (GCAB_COMPRESSION_NONE);
	for (i = 0; i < priv->filelist->len; i++) {
		_cleanup_free_ gchar *name = NULL;
		_cleanup_object_unref_ GCabFile *gfile = NULL;
		_cleanup_object_unref_ GFile *file_tmp = NULL;

		/* only write basename as name */
		tmp = g_ptr_array_index (priv->filelist, i);
		name = g_path_get_basename (tmp);

		/* add each file in turn */
		file_tmp = g_file_new_for_path (tmp);
		gfile = gcab_file_new_with_file (name, file_tmp);
		if (!gcab_folder_add_file (folder, gfile, FALSE,
					   cancellable, error))
			return FALSE;
	}
	if (!gcab_cabinet_add_folder (gcab, folder, error))
		return FALSE;

	/* write in one chunk */
	stream = G_OUTPUT_STREAM (g_file_replace (file, NULL, FALSE,
						  0, NULL, error));
	if (stream == NULL)
		return FALSE;
	if (!gcab_cabinet_write_simple (gcab, stream,
					NULL, NULL,
					cancellable, error))
		return FALSE;

	return TRUE;
}

/**
 * fu_cab_load_file:
 **/
gboolean
fu_cab_load_file (FuCab *cab, GFile *file, GCancellable *cancellable, GError **error)
{
	FuCabPrivate *priv = cab->priv;
	_cleanup_error_free_ GError *error_local = NULL;
	_cleanup_object_unref_ GFileInfo *info = NULL;

	g_return_val_if_fail (FU_IS_CAB (cab), FALSE);

	/* get size */
	info = g_file_query_info (file, G_FILE_ATTRIBUTE_STANDARD_SIZE,
				  G_FILE_QUERY_INFO_NONE, cancellable, &error_local);
	if (info == NULL) {
		_cleanup_free_ gchar *filename = NULL;
		filename = g_file_get_path (file);
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "Failed to get info for %s: %s",
			     filename, error_local->message);
		return FALSE;
	}
	priv->size = g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_STANDARD_SIZE);

	/* open file */
	priv->cab_stream = G_INPUT_STREAM (g_file_read (file, cancellable, &error_local));
	if (priv->cab_stream == NULL) {
		_cleanup_free_ gchar *filename = NULL;
		filename = g_file_get_path (file);
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "Failed to open %s: %s",
			     filename, error_local->message);
		return FALSE;
	}

	/* parse */
	return fu_cab_parse (cab, error);
}

/**
 * fu_cab_extract:
 **/
gboolean
fu_cab_extract (FuCab *cab, FuCabExtractFlags flags, GError **error)
{
	FuCabPrivate *priv = cab->priv;
	FuCabExtractHelper helper;
	_cleanup_error_free_ GError *error_local = NULL;
	_cleanup_object_unref_ GFile *path = NULL;

	g_return_val_if_fail (FU_IS_CAB (cab), FALSE);

	/* extract anything we need */
	helper.cab = cab;
	helper.flags = flags;
	path = g_file_new_for_path (priv->tmp_path);
	if (!gcab_cabinet_extract_simple (priv->gcab, path,
					  fu_cab_extract_cb, &helper,
					  NULL, &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_WRITE,
			     "failed to extract .cab file: %s",
			     error_local->message);
		return FALSE;
	}

	return TRUE;
}

/**
 * fu_cab_verify:
 **/
gboolean
fu_cab_verify (FuCab *cab, GError **error)
{
	FuCabPrivate *priv = cab->priv;
	_cleanup_error_free_ GError *error_local = NULL;
	_cleanup_free_ gchar *pki_dir = NULL;
	_cleanup_free_ gchar *signature = NULL;
	_cleanup_free_ gchar *fn = NULL;
	_cleanup_object_unref_ FuKeyring *kr = NULL;

	g_return_val_if_fail (FU_IS_CAB (cab), FALSE);

	/* no valid firmware extracted */
	if (priv->firmware_basename == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "%s not already extracted",
			     priv->firmware_basename);
		return FALSE;
	}

	/* check we were installed correctly */
	pki_dir = g_build_filename (SYSCONFDIR, "pki", "fwupd", NULL);
	if (!g_file_test (pki_dir, G_FILE_TEST_EXISTS)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_FOUND,
			     "PKI directory %s not found", pki_dir);
		return FALSE;
	}

	/* load signature */
	fn = g_build_filename (priv->tmp_path, priv->signature_basename, NULL);
	if (!g_file_test (fn, G_FILE_TEST_EXISTS)) {
		g_debug ("firmware archive contained no GPG signature");
		return TRUE;
	}
	if (!g_file_get_contents (fn, &signature, NULL, error))
		return FALSE;

	/* verify against the system trusted keys */
	kr = fu_keyring_new ();
	if (!fu_keyring_add_public_keys (kr, pki_dir, error))
		return FALSE;
	if (!fu_keyring_verify_file (kr, priv->firmware_filename,
				     signature, &error_local)) {
		g_warning ("untrusted as failed to verify: %s",
			   error_local->message);
	} else {
		g_debug ("marking payload as trusted");
		priv->trust_flags |= FWUPD_TRUST_FLAG_PAYLOAD;
	}

	return TRUE;
}

/**
 * fu_cab_delete_temp_files:
 **/
gboolean
fu_cab_delete_temp_files (FuCab *cab, GError **error)
{
	FuCabPrivate *priv = cab->priv;
	g_return_val_if_fail (FU_IS_CAB (cab), FALSE);

	if (priv->tmp_path != NULL) {
		const gchar *tmp;
		guint i;
		for (i = 0; i < priv->basenames_to_delete->len; i++) {
			_cleanup_free_ gchar *fn = NULL;
			tmp = g_ptr_array_index (priv->basenames_to_delete, i);
			fn = g_build_filename (priv->tmp_path, tmp, NULL);
			g_unlink (fn);
		}
		g_rmdir (priv->tmp_path);
		g_ptr_array_set_size (priv->basenames_to_delete, 0);
	}

	return TRUE;
}

/**
 * fu_cab_get_stream:
 **/
GInputStream *
fu_cab_get_stream (FuCab *cab)
{
	g_return_val_if_fail (FU_IS_CAB (cab), NULL);
	return cab->priv->cab_stream;
}

/**
 * fu_cab_get_guid:
 **/
const gchar *
fu_cab_get_guid (FuCab *cab)
{
	g_return_val_if_fail (FU_IS_CAB (cab), NULL);
	return cab->priv->guid;
}

/**
 * fu_cab_get_version:
 **/
const gchar *
fu_cab_get_version (FuCab *cab)
{
	g_return_val_if_fail (FU_IS_CAB (cab), NULL);
	return cab->priv->version;
}

/**
 * fu_cab_get_vendor:
 **/
const gchar *
fu_cab_get_vendor (FuCab *cab)
{
	g_return_val_if_fail (FU_IS_CAB (cab), NULL);
	return cab->priv->vendor;
}

/**
 * fu_cab_get_name:
 **/
const gchar *
fu_cab_get_name (FuCab *cab)
{
	g_return_val_if_fail (FU_IS_CAB (cab), NULL);
	return cab->priv->name;
}

/**
 * fu_cab_get_summary:
 **/
const gchar *
fu_cab_get_summary (FuCab *cab)
{
	g_return_val_if_fail (FU_IS_CAB (cab), NULL);
	return cab->priv->summary;
}

/**
 * fu_cab_get_description:
 **/
const gchar *
fu_cab_get_description (FuCab *cab)
{
	g_return_val_if_fail (FU_IS_CAB (cab), NULL);
	return cab->priv->description;
}

/**
 * fu_cab_get_url_homepage:
 **/
const gchar *
fu_cab_get_url_homepage (FuCab *cab)
{
	g_return_val_if_fail (FU_IS_CAB (cab), NULL);
	return cab->priv->url_homepage;
}

/**
 * fu_cab_get_size:
 **/
guint64
fu_cab_get_size (FuCab *cab)
{
	g_return_val_if_fail (FU_IS_CAB (cab), 0);
	return cab->priv->size;
}

/**
 * fu_cab_get_license:
 **/
const gchar *
fu_cab_get_license (FuCab *cab)
{
	g_return_val_if_fail (FU_IS_CAB (cab), NULL);
	return cab->priv->license;
}

/**
 * fu_cab_get_filename_firmware:
 **/
const gchar *
fu_cab_get_filename_firmware (FuCab *cab)
{
	g_return_val_if_fail (FU_IS_CAB (cab), NULL);
	return cab->priv->firmware_filename;
}

/**
 * fu_cab_get_trust_flags:
 **/
FwupdTrustFlags
fu_cab_get_trust_flags (FuCab *cab)
{
	g_return_val_if_fail (FU_IS_CAB (cab), 0);
	return cab->priv->trust_flags;
}

/**
 * fu_cab_class_init:
 **/
static void
fu_cab_class_init (FuCabClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = fu_cab_finalize;
	g_type_class_add_private (klass, sizeof (FuCabPrivate));
}

/**
 * fu_cab_init:
 **/
static void
fu_cab_init (FuCab *cab)
{
	cab->priv = FU_CAB_GET_PRIVATE (cab);
	cab->priv->basenames_to_delete = g_ptr_array_new_with_free_func (g_free);
	cab->priv->filelist = g_ptr_array_new_with_free_func (g_free);
}

/**
 * fu_cab_finalize:
 **/
static void
fu_cab_finalize (GObject *object)
{
	FuCab *cab = FU_CAB (object);
	FuCabPrivate *priv = cab->priv;

	g_free (priv->firmware_basename);
	g_free (priv->firmware_filename);
	g_free (priv->signature_basename);
	g_free (priv->cat_basename);
	g_free (priv->guid);
	g_free (priv->inf_basename);
	g_free (priv->metainfo_basename);
	g_free (priv->tmp_path);
	g_free (priv->name);
	g_free (priv->summary);
	g_free (priv->description);
	g_free (priv->url_homepage);
	g_free (priv->vendor);
	g_free (priv->version);
	g_free (priv->license);
	if (priv->cab_stream != NULL)
		g_object_unref (priv->cab_stream);
	if (priv->gcab != NULL)
		g_object_unref (priv->gcab);
	if (priv->inf_kf != NULL)
		g_key_file_unref (priv->inf_kf);
	g_ptr_array_unref (priv->basenames_to_delete);
	g_ptr_array_unref (priv->filelist);

	G_OBJECT_CLASS (fu_cab_parent_class)->finalize (object);
}

/**
 * fu_cab_new:
 **/
FuCab *
fu_cab_new (void)
{
	FuCab *cab;
	cab = g_object_new (FU_TYPE_CAB, NULL);
	return FU_CAB (cab);
}
