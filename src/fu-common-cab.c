/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN				"FuCommonCab"

#include "config.h"

#include <libgcab.h>

#include "fu-common-cab.h"
#include "fu-common.h"

#include "fwupd-error.h"

#ifndef HAVE_GCAB_1_0
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GCabCabinet, g_object_unref)
#endif

static GCabFile *
_gcab_cabinet_get_file_by_name (GCabCabinet *cabinet, const gchar *basename)
{
	GPtrArray *folders = gcab_cabinet_get_folders (cabinet);
	for (guint i = 0; i < folders->len; i++) {
		GCabFolder *cabfolder = GCAB_FOLDER (g_ptr_array_index (folders, i));
#ifdef HAVE_GCAB_1_0
		GCabFile *cabfile = gcab_folder_get_file_by_name (cabfolder, basename);
		if (cabfile != NULL)
			return cabfile;
#else
		g_autoptr(GSList) files = gcab_folder_get_files (cabfolder);
		for (GSList *l = files; l != NULL; l = l->next) {
			GCabFile *cabfile = GCAB_FILE (l->data);
			if (g_strcmp0 (gcab_file_get_name (cabfile), basename) == 0)
				return cabfile;
		}
#endif
	}
	return NULL;
}

#ifndef HAVE_GCAB_1_0
static GBytes *
_gcab_file_get_bytes (GCabFile *cabfile)
{
	GBytes *blob = NULL;
	g_autofree gchar *fn = NULL;
	g_autoptr(GError) error_local = NULL;

	fn = g_build_filename (g_object_get_data (G_OBJECT (cabfile),
						  "fwupd::DecompressPath"),
			       gcab_file_get_extract_name (cabfile),
			       NULL);
	blob = fu_common_get_contents_bytes (fn, &error_local);
	if (blob == NULL) {
		g_warning ("failed to read temp file: %s", error_local->message);
		return NULL;
	}
	return blob;
}
#endif

/* sets the firmware and signature blobs on XbNode */
static gboolean
fu_common_store_from_cab_release (XbNode *release, GCabCabinet *cabinet, GError **error)
{
	GCabFile *cabfile;
	GBytes *blob;
	const gchar *csum_filename;
	const gchar *suffixes[] = { "asc", "p7b", "p7c", NULL };
	g_autofree gchar *basename = NULL;
	g_autofree gchar *release_key = NULL;
	g_autoptr(XbNode) csum_tmp = NULL;
	g_autoptr(XbNode) nsize = NULL;

	/* ensure we always have a content checksum */
	csum_tmp = xb_node_query_first (release, "checksum[@target='content']", NULL);
	if (csum_tmp != NULL) {
		csum_filename = xb_node_get_attr (csum_tmp, "filename");
	} else {
		/* if this isn't true, a firmware needs to set in
		 * the metainfo.xml file something like:
		 * <checksum target="content" filename="FLASH.ROM"/> */
		csum_filename = "firmware.bin";
	}

	/* get the main firmware file */
	basename = g_path_get_basename (csum_filename);
	cabfile = _gcab_cabinet_get_file_by_name (cabinet, basename);
	if (cabfile == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "cannot find %s in archive",
			     basename);
		return FALSE;
	}
#ifdef HAVE_GCAB_1_0
	blob = gcab_file_get_bytes (cabfile);
#else
	blob = _gcab_file_get_bytes (cabfile);
#endif
	if (blob == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "no GBytes from GCabFile firmware");
		return FALSE;
	}

	/* set the blob */
	release_key = g_strdup_printf ("fwupd::ReleaseBlob(%s)", basename);
	xb_node_set_data (release, release_key, blob);

	/* set as metadata if unset, but error if specified and incorrect */
	nsize = xb_node_query_first (release, "size[@type='installed']", NULL);
	if (nsize != NULL) {
		guint64 size = fu_common_strtoull (xb_node_get_text (nsize));
		if (size != g_bytes_get_size (blob)) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "contents size invalid, expected "
				     "%" G_GSIZE_FORMAT ", got %" G_GUINT64_FORMAT,
				     g_bytes_get_size (blob), size);
			return FALSE;
		}
	} else {
		guint64 size = g_bytes_get_size (blob);
		g_autoptr(GBytes) blob_sz = g_bytes_new (&size, sizeof(guint64));
		xb_node_set_data (release, "fwupd::ReleaseSize", blob_sz);
	}

	/* set if unspecified, but error out if specified and incorrect */
	if (csum_tmp != NULL && xb_node_get_text (csum_tmp) != NULL) {
		g_autofree gchar *checksum = NULL;
		checksum = g_compute_checksum_for_bytes (G_CHECKSUM_SHA1, blob);
		if (g_strcmp0 (checksum, xb_node_get_text (csum_tmp)) != 0) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "contents checksum invalid, expected %s, got %s",
				     checksum,
				     xb_node_get_text (csum_tmp));
			return FALSE;
		}
	}

	/* if the signing file exists, set that too */
	for (guint i = 0; suffixes[i] != NULL; i++) {
		g_autofree gchar *basename_sig = NULL;
		basename_sig = g_strdup_printf ("%s.%s", basename, suffixes[i]);
		cabfile = _gcab_cabinet_get_file_by_name (cabinet, basename_sig);
		if (cabfile != NULL) {
			g_autofree gchar *release_key_sig = NULL;
#ifdef HAVE_GCAB_1_0
			blob = gcab_file_get_bytes (cabfile);
#else
			blob = _gcab_file_get_bytes (cabfile);
#endif
			if (blob == NULL) {
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INVALID_FILE,
					     "no GBytes from GCabFile %s",
					     basename_sig);
				return FALSE;
			}
			release_key_sig = g_strdup_printf ("fwupd::ReleaseBlob(%s)",
							   basename_sig);
			xb_node_set_data (release, release_key_sig, blob);
		}
	}

	/* success */
	return TRUE;
}

/* adds each GCabFile to the silo */
static gboolean
fu_common_store_from_cab_file (XbBuilder *builder, GCabCabinet *cabinet,
			       GCabFile *cabfile, GError **error)
{
	GBytes *blob;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(XbBuilderSource) source = xb_builder_source_new ();

	/* parse file */
#ifdef HAVE_GCAB_1_0
	blob = gcab_file_get_bytes (cabfile);
#else
	blob = _gcab_file_get_bytes (cabfile);
#endif
	if (blob == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "no GBytes from GCabFile");
		return FALSE;
	}
	if (!xb_builder_source_load_xml (source,
					 g_bytes_get_data (blob, NULL),
					 XB_BUILDER_SOURCE_FLAG_NONE,
					 &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "could not parse MetaInfo XML: %s",
			     error_local->message);
		return FALSE;
	}
	xb_builder_import_source (builder, source);

	/* success */
	return TRUE;
}

/* adds each GCabFolder to the silo */
static gboolean
fu_common_store_from_cab_folder (XbBuilder *builder, GCabCabinet *cabinet,
				 GCabFolder *cabfolder, GError **error)
{
	g_autoptr(GSList) cabfiles = gcab_folder_get_files (cabfolder);
	for (GSList *l = cabfiles; l != NULL; l = l->next) {
		GCabFile *cabfile = GCAB_FILE (l->data);
		const gchar *fn = gcab_file_get_extract_name (cabfile);
		g_debug ("processing file: %s", fn);
		if (g_str_has_suffix (fn, ".metainfo.xml")) {
			if (!fu_common_store_from_cab_file (builder, cabinet, cabfile, error)) {
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
	const gchar	*decompress_path;
	GError		*error;
} FuCommonCabHelper;

static gboolean
fu_common_store_file_cb (GCabFile *file, gpointer user_data)
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

#ifndef HAVE_GCAB_1_0
	/* set this for old versions of GCab */
	g_object_set_data_full (G_OBJECT (file),
				"fwupd::DecompressPath",
				g_strdup (helper->decompress_path),
				g_free);
#endif

	return TRUE;
}

/**
 * fu_common_cab_build_silo:
 * @blob: A readable blob
 * @size_max: The maximum size of the archive
 * @error: A #FuEndianType, e.g. %G_LITTLE_ENDIAN
 *
 * Create an AppStream silo from a cabinet archive.
 *
 * Returns: a #XbSilo, or %NULL on error
 **/
XbSilo *
fu_common_cab_build_silo (GBytes *blob, guint64 size_max, GError **error)
{
	FuCommonCabHelper helper = {
		.size_total	= 0,
		.size_max	= size_max,
		.error		= NULL,
	};
	GPtrArray *folders;
#ifndef HAVE_GCAB_1_0
	g_autofree gchar *tmp_path = NULL;
	g_autoptr(GFile) tmp_file = NULL;
#endif
	g_autoptr(XbSilo) silo = NULL;
	g_autoptr(XbBuilder) builder = xb_builder_new ();
	g_autoptr(GCabCabinet) cabinet = gcab_cabinet_new ();
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GInputStream) ip = NULL;
	g_autoptr(GPtrArray) components = NULL;

	/* load from a seekable stream */
	ip = g_memory_input_stream_new_from_bytes (blob);
	if (!gcab_cabinet_load (cabinet, ip, NULL, error))
		return NULL;

#ifdef HAVE_GCAB_1_0
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
	if (!gcab_cabinet_extract_simple (cabinet, NULL,
					  fu_common_store_file_cb, &helper,
					  NULL, &error_local)) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     error_local->message);
		return NULL;
	}
#else
	/* decompress to /tmp */
	tmp_path = g_dir_make_tmp ("fwupd-XXXXXX", &error_local);
	if (tmp_path == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "failed to create temp dir: %s",
			     error_local->message);
		return FALSE;
	}
	helper.decompress_path = tmp_path;
	tmp_file = g_file_new_for_path (tmp_path);
	if (!gcab_cabinet_extract_simple (cabinet, tmp_file,
					  fu_common_store_file_cb, &helper,
					  NULL, &error_local)) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     error_local->message);
		return NULL;
	}
#endif

	/* the file callback set an error */
	if (helper.error != NULL) {
		g_propagate_error (error, helper.error);
		return NULL;
	}

	/* verbose profiling */
	if (g_getenv ("FWUPD_VERBOSE") != NULL) {
		xb_builder_set_profile_flags (builder,
					      XB_SILO_PROFILE_FLAG_XPATH |
					      XB_SILO_PROFILE_FLAG_DEBUG);
	}

	/* look at each folder */
	folders = gcab_cabinet_get_folders (cabinet);
	for (guint i = 0; i < folders->len; i++) {
		GCabFolder *cabfolder = GCAB_FOLDER (g_ptr_array_index (folders, i));
		g_debug ("processing folder: %u/%u", i + 1, folders->len);
		if (!fu_common_store_from_cab_folder (builder, cabinet, cabfolder, error))
			return NULL;
	}

	/* did we get any valid files */
	silo = xb_builder_compile (builder, XB_BUILDER_COMPILE_FLAG_NONE, NULL, error);
	if (silo == NULL)
		return NULL;

	/* this looks weird, but metainfo files have no <components> node */
	components = xb_silo_query (silo, "component", 0, &error_local);
	if (components == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "archive contained no valid metadata: %s",
			     error_local->message);
		return NULL;
	}

	/* process each listed release */
	for (guint i = 0; i < components->len; i++) {
		XbNode *component = g_ptr_array_index (components, i);
		g_autoptr(GPtrArray) releases = NULL;
		releases = xb_node_query (component, "releases/release", 0, &error_local);
		if (releases == NULL) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "no releases in metainfo file: %s",
				     error_local->message);
			return NULL;
		}
		for (guint j = 0; j < releases->len; j++) {
			XbNode *rel = g_ptr_array_index (releases, j);
			g_debug ("processing release: %s", xb_node_get_attr (rel, "version"));
			if (!fu_common_store_from_cab_release (rel, cabinet, error))
				return NULL;
		}
	}

	/* success */
	return g_steal_pointer (&silo);
}
