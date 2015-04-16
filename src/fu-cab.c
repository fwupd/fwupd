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
	gchar				*signature_filename;
	gchar				*cat_basename;
	gchar				*cat_filename;
	gchar				*description;
	gchar				*guid;
	gchar				*inf_filename;
	gchar				*metainfo_filename;
	gchar				*tmp_path;
	gchar				*license;
	gchar				*name;
	gchar				*summary;
	gchar				*url_homepage;
	gchar				*vendor;
	gchar				*version;
	guint64				 size;
};

G_DEFINE_TYPE (FuCab, fu_cab, G_TYPE_OBJECT)

/**
 * fu_cab_extract_inf_cb:
 **/
static gboolean
fu_cab_extract_inf_cb (GCabFile *file, gpointer user_data)
{
	FuCab *cab = FU_CAB (user_data);
	FuCabPrivate *priv = cab->priv;

	/* only extract the first .inf file found in the .cab file */
	if (priv->inf_filename == NULL &&
	    g_str_has_suffix (gcab_file_get_name (file), ".inf")) {
		priv->inf_filename = g_build_filename (priv->tmp_path,
							 gcab_file_get_name (file),
							 NULL);
		return TRUE;
	}

	/* also extract the optional metainfo file if it exists */
	if (priv->metainfo_filename == NULL &&
	    g_str_has_suffix (gcab_file_get_name (file), ".metainfo.xml")) {
		priv->metainfo_filename = g_build_filename (priv->tmp_path,
							    gcab_file_get_name (file),
							    NULL);
		return TRUE;
	}
	return FALSE;
}

/**
 * fu_cab_extract_firmware_cb:
 **/
static gboolean
fu_cab_extract_firmware_cb (GCabFile *file, gpointer user_data)
{
	FuCab *cab = FU_CAB (user_data);
	FuCabPrivate *priv = cab->priv;

	/* only extract the firmware file listed in the .inf file */
	if (priv->firmware_filename == NULL &&
	    g_strcmp0 (gcab_file_get_name (file),
		       priv->firmware_basename) == 0) {
		priv->firmware_filename = g_build_filename (priv->tmp_path,
							    gcab_file_get_name (file),
							    NULL);
		return TRUE;
	}
	if (priv->signature_filename == NULL &&
	    g_strcmp0 (gcab_file_get_name (file),
		       priv->signature_basename) == 0) {
		priv->signature_filename = g_build_filename (priv->tmp_path,
							     gcab_file_get_name (file),
							     NULL);
		return TRUE;
	}
	if (priv->cat_filename == NULL &&
	    g_strcmp0 (gcab_file_get_name (file),
		       priv->cat_basename) == 0) {
		priv->cat_filename = g_build_filename (priv->tmp_path,
						       gcab_file_get_name (file),
						       NULL);
		return TRUE;
	}
	return FALSE;
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
	_cleanup_error_free_ GError *error_local = NULL;
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

	path = g_file_new_for_path (priv->tmp_path);
	if (!gcab_cabinet_extract_simple (priv->gcab, path,
					  fu_cab_extract_inf_cb,
					  cab, NULL, &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_WRITE,
			     "failed to extract .cab file: %s",
			     error_local->message);
		return FALSE;
	}

	/* read .inf file */
	if (priv->inf_filename == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "no .inf file in.cab file");
		return FALSE;
	}

	/* parse it */
	app = as_app_new ();
	if (!as_app_parse_file (app, priv->inf_filename,
				AS_APP_PARSE_FLAG_NONE, &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "%s could not be loaded: %s",
			     priv->inf_filename,
			     error_local->message);
		return FALSE;
	}

	/* merge with the metainfo file */
	if (priv->metainfo_filename != NULL) {
		_cleanup_object_unref_ AsApp *app2 = NULL;
		app2 = as_app_new ();
		if (!as_app_parse_file (app2, priv->metainfo_filename,
					AS_APP_PARSE_FLAG_NONE, &error_local)) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "%s could not be loaded: %s",
				     priv->metainfo_filename,
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
 * fu_cab_extract_firmware:
 **/
gboolean
fu_cab_extract_firmware (FuCab *cab, GError **error)
{
	FuCabPrivate *priv = cab->priv;
	_cleanup_error_free_ GError *error_local = NULL;
	_cleanup_object_unref_ GFile *path = NULL;

	g_return_val_if_fail (FU_IS_CAB (cab), FALSE);

	/* no valid firmware file */
	if (priv->firmware_basename == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "no firmware found in cab file");
		return FALSE;
	}

	/* now extract the firmware */
	g_debug ("extracting %s", priv->firmware_basename);
	path = g_file_new_for_path (priv->tmp_path);
	if (!gcab_cabinet_extract_simple (priv->gcab, path,
					  fu_cab_extract_firmware_cb,
					  cab, NULL, &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_WRITE,
			     "failed to extract .cab file: %s",
			     error_local->message);
		return FALSE;
	}
	if (priv->firmware_filename == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "%s not found in cab file",
			     priv->firmware_basename);
		return FALSE;
	}

	/* check signature */
	if (priv->signature_filename != NULL) {
		_cleanup_object_unref_ FuKeyring *kr = NULL;
		_cleanup_free_ gchar *pki_dir = NULL;
		_cleanup_free_ gchar *signature = NULL;

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
		if (!g_file_get_contents (priv->signature_filename,
					  &signature, NULL, error))
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
	} else {
		g_debug ("firmware archive contained no signatures");
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

	if (priv->inf_filename != NULL)
		g_unlink (priv->inf_filename);
	if (priv->firmware_filename != NULL)
		g_unlink (priv->firmware_filename);
	if (priv->signature_filename != NULL)
		g_unlink (priv->signature_filename);
	if (priv->tmp_path != NULL)
		g_rmdir (priv->tmp_path);

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
	g_free (priv->signature_filename);
	g_free (priv->cat_basename);
	g_free (priv->cat_filename);
	g_free (priv->guid);
	g_free (priv->inf_filename);
	g_free (priv->metainfo_filename);
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
