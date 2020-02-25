/*
 * Copyright (C) 2017-2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN				"FuCabinet"

#include "config.h"

#include <gio/gio.h>
#include <libgcab.h>

#include "fu-cabinet.h"
#include "fu-common.h"

#include "fwupd-enums.h"
#include "fwupd-error.h"

struct _FuCabinet {
	GObject			 parent_instance;
	guint64			 size_max;
	GCabCabinet		*gcab_cabinet;
	gchar			*container_checksum;
	XbBuilder		*builder;
	XbSilo			*silo;
	JcatContext		*jcat_context;
	JcatFile		*jcat_file;
};

G_DEFINE_TYPE (FuCabinet, fu_cabinet, G_TYPE_OBJECT)

static void
fu_cabinet_finalize (GObject *obj)
{
	FuCabinet *self = FU_CABINET (obj);
	if (self->silo != NULL)
		g_object_unref (self->silo);
	if (self->builder != NULL)
		g_object_unref (self->builder);
	g_free (self->container_checksum);
	g_object_unref (self->gcab_cabinet);
	g_object_unref (self->jcat_context);
	g_object_unref (self->jcat_file);
	G_OBJECT_CLASS (fu_cabinet_parent_class)->finalize (obj);
}

static void
fu_cabinet_class_init (FuCabinetClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = fu_cabinet_finalize;
}

static void
fu_cabinet_init (FuCabinet *self)
{
	self->size_max = 1024 * 1024 * 100;
	self->gcab_cabinet = gcab_cabinet_new ();
	self->builder = xb_builder_new ();
	self->jcat_file = jcat_file_new ();
	self->jcat_context = jcat_context_new ();
}

/**
 * fu_cabinet_set_size_max:
 * @self: A #FuCabinet
 * @size_max: size in bytes
 *
 * Sets the maximum size of the decompressed cabinet file.
 *
 * Since: 1.4.0
 **/
void
fu_cabinet_set_size_max (FuCabinet *self, guint64 size_max)
{
	g_return_if_fail (FU_IS_CABINET (self));
	self->size_max = size_max;
}

/**
 * fu_cabinet_set_jcat_context: (skip):
 * @self: A #FuCabinet
 * @jcat_context: (nullable): A #JcatContext
 *
 * Sets the Jcat context, which is used for setting the trust flags on the
 * each release in the archive.
 *
 * Since: 1.4.0
 **/
void
fu_cabinet_set_jcat_context (FuCabinet *self, JcatContext *jcat_context)
{
	g_return_if_fail (FU_IS_CABINET (self));
	g_return_if_fail (JCAT_IS_CONTEXT (jcat_context));
	g_set_object (&self->jcat_context, jcat_context);
}

/**
 * fu_cabinet_get_silo: (skip):
 * @self: A #FuCabinet
 *
 * Gets the silo that represents the supset metadata of all the metainfo files
 * found in the archive.
 *
 * Returns: (transfer full): a #XbSilo, or %NULL if the archive has not been parsed
 *
 * Since: 1.4.0
 **/
XbSilo *
fu_cabinet_get_silo (FuCabinet *self)
{
	g_return_val_if_fail (FU_IS_CABINET (self), NULL);
	if (self->silo == NULL)
		return NULL;
	return g_object_ref (self->silo);
}

static GCabFile *
fu_cabinet_get_file_by_name (FuCabinet *self, const gchar *basename)
{
	GPtrArray *folders = gcab_cabinet_get_folders (self->gcab_cabinet);
	for (guint i = 0; i < folders->len; i++) {
		GCabFolder *cabfolder = GCAB_FOLDER (g_ptr_array_index (folders, i));
		GCabFile *cabfile = gcab_folder_get_file_by_name (cabfolder, basename);
		if (cabfile != NULL)
			return cabfile;
	}
	return NULL;
}

/* sets the firmware and signature blobs on XbNode */
static gboolean
fu_cabinet_parse_release (FuCabinet *self, XbNode *release, GError **error)
{
	GCabFile *cabfile;
	GBytes *blob;
	const gchar *csum_filename = NULL;
	g_autofree gchar *basename = NULL;
	g_autoptr(XbNode) csum_tmp = NULL;
	g_autoptr(XbNode) metadata_trust = NULL;
	g_autoptr(XbNode) nsize = NULL;
	g_autoptr(JcatItem) item = NULL;
	g_autoptr(GBytes) release_flags_blob = NULL;
	FwupdReleaseFlags release_flags = FWUPD_RELEASE_FLAG_NONE;

	/* we set this with XbBuilderSource before the silo was created */
	metadata_trust = xb_node_query_first (release, "../../info/metadata_trust", NULL);
	if (metadata_trust != NULL)
		release_flags |= FWUPD_RELEASE_FLAG_TRUSTED_METADATA;

	/* ensure we always have a content checksum */
	csum_tmp = xb_node_query_first (release, "checksum[@target='content']", NULL);
	if (csum_tmp != NULL)
		csum_filename = xb_node_get_attr (csum_tmp, "filename");

	/* if this isn't true, a firmware needs to set in the metainfo.xml file
	 * something like: <checksum target="content" filename="FLASH.ROM"/> */
	if (csum_filename == NULL)
		csum_filename = "firmware.bin";

	/* get the main firmware file */
	basename = g_path_get_basename (csum_filename);
	cabfile = fu_cabinet_get_file_by_name (self, basename);
	if (cabfile == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "cannot find %s in archive",
			     basename);
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
	xb_node_set_data (release, "fwupd::FirmwareBlob", blob);

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

	/* find out if the payload is signed, falling back to detached */
	item = jcat_file_get_item_by_id (self->jcat_file, basename, NULL);
	if (item != NULL) {
		g_autoptr(GError) error_local = NULL;
		g_autoptr(GPtrArray) results = NULL;
		results = jcat_context_verify_item (self->jcat_context,
						    blob, item,
						    JCAT_VERIFY_FLAG_REQUIRE_CHECKSUM |
						    JCAT_VERIFY_FLAG_REQUIRE_SIGNATURE,
						    &error_local);
		if (results == NULL) {
			g_debug ("failed to verify payload %s: %s",
				 basename, error_local->message);
		} else {
			g_debug ("verified payload %s: %u",
				 basename, results->len);
			release_flags |= FWUPD_RELEASE_FLAG_TRUSTED_PAYLOAD;
		}

	/* legacy GPG detached signature */
	} else {
		g_autofree gchar *basename_sig = NULL;
		basename_sig = g_strdup_printf ("%s.asc", basename);
		cabfile = fu_cabinet_get_file_by_name (self, basename_sig);
		if (cabfile != NULL) {
			GBytes *data_sig;
			g_autoptr(JcatResult) jcat_result = NULL;
			g_autoptr(JcatBlob) jcat_blob = NULL;
			g_autoptr(GError) error_local = NULL;

			data_sig = gcab_file_get_bytes (cabfile);
			if (data_sig == NULL) {
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INVALID_FILE,
					     "no GBytes from GCabFile %s",
					     basename_sig);
				return FALSE;
			}
			jcat_blob = jcat_blob_new (JCAT_BLOB_KIND_GPG, data_sig);
			jcat_result = jcat_context_verify_blob (self->jcat_context,
								blob, jcat_blob,
								JCAT_VERIFY_FLAG_REQUIRE_SIGNATURE,
								&error_local);
			if (jcat_result == NULL) {
				g_debug ("failed to verify payload %s using detached: %s",
					 basename, error_local->message);
			} else {
				g_debug ("verified payload %s using detached", basename);
				release_flags |= FWUPD_RELEASE_FLAG_TRUSTED_PAYLOAD;
			}
		}
	}

	/* this means we can get the data from fu_keyring_get_release_flags */
	release_flags_blob = g_bytes_new (&release_flags, sizeof(release_flags));
	xb_node_set_data (release, "fwupd::ReleaseFlags", release_flags_blob);

	/* success */
	return TRUE;
}

static gint
fu_cabinet_sort_cb (XbBuilderNode *bn1, XbBuilderNode *bn2, gpointer user_data)
{
	guint64 prio1 = xb_builder_node_get_attr_as_uint (bn1, "priority");
	guint64 prio2 = xb_builder_node_get_attr_as_uint (bn2, "priority");
	if (prio1 > prio2)
		return -1;
	if (prio1 < prio2)
		return 1;
	return 0;
}

static gboolean
fu_cabinet_sort_priority_cb (XbBuilderFixup *self,
			     XbBuilderNode *bn,
			     gpointer user_data,
			     GError **error)
{
	xb_builder_node_sort_children (bn, fu_cabinet_sort_cb, user_data);
	return TRUE;
}

static XbBuilderNode *
_xb_builder_node_get_child_by_element_attr (XbBuilderNode *bn,
					    const gchar *element,
					    const gchar *attr_name,
					    const gchar *attr_value)
{
	GPtrArray *bcs = xb_builder_node_get_children (bn);
	for (guint i = 0; i < bcs->len; i++) {
		XbBuilderNode *bc = g_ptr_array_index (bcs, i);
		if (g_strcmp0 (xb_builder_node_get_element (bc), element) != 0)
			continue;
		if (g_strcmp0 (xb_builder_node_get_attr (bc, attr_name), attr_value) == 0)
			return g_object_ref (bc);
	}
	return NULL;
}

static gboolean
fu_cabinet_set_container_checksum_cb (XbBuilderFixup *builder_fixup,
				      XbBuilderNode *bn,
				      gpointer user_data,
				      GError **error)
{
	FuCabinet *self = FU_CABINET (user_data);
	g_autoptr(XbBuilderNode) csum = NULL;

	/* not us */
	if (g_strcmp0 (xb_builder_node_get_element (bn), "release") != 0)
		return TRUE;

	/* verify it exists */
	csum = _xb_builder_node_get_child_by_element_attr (bn, "checksum",
							   "type", "container");
	if (csum == NULL) {
		csum = xb_builder_node_insert (bn, "checksum",
					       "target", "container",
					       NULL);
	}

	/* verify it is correct */
	if (g_strcmp0 (xb_builder_node_get_text (csum), self->container_checksum) != 0) {
		g_debug ("invalid container checksum %s, fixing up to %s",
			 xb_builder_node_get_text (csum), self->container_checksum);
		xb_builder_node_set_text (csum, self->container_checksum, -1);
	}
	return TRUE;
}

/* adds each GCabFile to the silo */
static gboolean
fu_cabinet_build_silo_file (FuCabinet *self,
			    GCabFile *cabfile,
			    FwupdReleaseFlags release_flags,
			    GError **error)
{
	GBytes *blob;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(XbBuilderSource) source = xb_builder_source_new ();
	g_autoptr(XbBuilderNode) bn_info = xb_builder_node_new ("info");

	/* indicate the metainfo file was signed */
	if (release_flags & FWUPD_RELEASE_FLAG_TRUSTED_METADATA) {
		g_autoptr(XbBuilderNode) bn_trust = NULL;
		xb_builder_node_insert (bn_info, "metadata_trust", NULL);
	}
	xb_builder_source_set_info (source, bn_info);

	/* rewrite to be under a components root */
	xb_builder_source_set_prefix (source, "components");

	/* parse file */
	blob = gcab_file_get_bytes (cabfile);
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
	xb_builder_import_source (self->builder, source);

	/* success */
	return TRUE;
}

static gboolean
fu_cabinet_build_silo_metainfo (FuCabinet *self, GCabFile *cabfile, GError **error)
{
	FwupdReleaseFlags release_flags = FWUPD_RELEASE_FLAG_NONE;
	const gchar *fn = gcab_file_get_extract_name (cabfile);
	g_autoptr(JcatItem) item = NULL;

	/* validate against the Jcat file */
	item = jcat_file_get_item_by_id (self->jcat_file, fn, NULL);
	if (item == NULL) {
		g_debug ("failed to verify %s: no JcatItem", fn);
	} else {
		g_autoptr(GError) error_local = NULL;
		g_autoptr(GPtrArray) results = NULL;
		results = jcat_context_verify_item (self->jcat_context,
						    gcab_file_get_bytes (cabfile),
						    item,
						    JCAT_VERIFY_FLAG_REQUIRE_CHECKSUM |
						    JCAT_VERIFY_FLAG_REQUIRE_SIGNATURE,
						    &error_local);
		if (results == NULL) {
			g_debug ("failed to verify %s: %s",
				 fn, error_local->message);
		} else {
			g_debug ("verified metadata %s: %u",
				 fn, results->len);
			release_flags |= FWUPD_RELEASE_FLAG_TRUSTED_METADATA;
		}
	}

	/* actually parse the XML now */
	g_debug ("processing file: %s", fn);
	if (!fu_cabinet_build_silo_file (self, cabfile, release_flags, error)) {
		g_prefix_error (error, "%s could not be loaded: ",
				gcab_file_get_extract_name (cabfile));
		return FALSE;
	}

	/* success */
	return TRUE;
}

/* load the firmware.jcat files if included */
static gboolean
fu_cabinet_build_jcat_folder (FuCabinet *self, GCabFolder *cabfolder, GError **error)
{
	g_autoptr(GSList) cabfiles = gcab_folder_get_files (cabfolder);
	for (GSList *l = cabfiles; l != NULL; l = l->next) {
		GCabFile *cabfile = GCAB_FILE (l->data);
		const gchar *fn = gcab_file_get_extract_name (cabfile);
		if (g_str_has_suffix (fn, ".jcat")) {
			GBytes *data_jcat = gcab_file_get_bytes (cabfile);
			g_autoptr(GInputStream) istream = NULL;
			istream = g_memory_input_stream_new_from_bytes (data_jcat);
			if (!jcat_file_import_stream (self->jcat_file,
						      istream,
						      JCAT_IMPORT_FLAG_NONE,
						      NULL,
						      error))
				return FALSE;
		}
	}
	return TRUE;
}

/* adds each GCabFolder to the silo */
static gboolean
fu_cabinet_build_silo_folder (FuCabinet *self, GCabFolder *cabfolder, GError **error)
{
	g_autoptr(GSList) cabfiles = gcab_folder_get_files (cabfolder);
	for (GSList *l = cabfiles; l != NULL; l = l->next) {
		GCabFile *cabfile = GCAB_FILE (l->data);
		const gchar *fn = gcab_file_get_extract_name (cabfile);
		if (!g_str_has_suffix (fn, ".metainfo.xml"))
			continue;
		if (!fu_cabinet_build_silo_metainfo (self, cabfile, error))
			return FALSE;
	}
	return TRUE;
}

static gboolean
fu_cabinet_build_silo (FuCabinet *self, GBytes *data, GError **error)
{
	GPtrArray *folders;
	g_autoptr(XbBuilderFixup) fixup1 = NULL;
	g_autoptr(XbBuilderFixup) fixup2 = NULL;

	/* verbose profiling */
	if (g_getenv ("FWUPD_VERBOSE") != NULL) {
		xb_builder_set_profile_flags (self->builder,
					      XB_SILO_PROFILE_FLAG_XPATH |
					      XB_SILO_PROFILE_FLAG_DEBUG);
	}

	/* load Jcat */
	folders = gcab_cabinet_get_folders (self->gcab_cabinet);
	if (self->jcat_context != NULL) {
		for (guint i = 0; i < folders->len; i++) {
			GCabFolder *cabfolder = GCAB_FOLDER (g_ptr_array_index (folders, i));
			if (!fu_cabinet_build_jcat_folder (self, cabfolder, error))
				return FALSE;
		}
	}

	/* adds each metainfo file to the silo */
	for (guint i = 0; i < folders->len; i++) {
		GCabFolder *cabfolder = GCAB_FOLDER (g_ptr_array_index (folders, i));
		g_debug ("processing folder: %u/%u", i + 1, folders->len);
		if (!fu_cabinet_build_silo_folder (self, cabfolder, error))
			return FALSE;
	}

	/* sort the components by priority */
	fixup1 = xb_builder_fixup_new ("OrderByPriority",
				       fu_cabinet_sort_priority_cb,
				       NULL, NULL);
	xb_builder_fixup_set_max_depth (fixup1, 0);
	xb_builder_add_fixup (self->builder, fixup1);

	/* ensure the container checksum is always set */
	fixup2 = xb_builder_fixup_new ("SetContainerChecksum",
				       fu_cabinet_set_container_checksum_cb,
				       self, NULL);
	xb_builder_add_fixup (self->builder, fixup2);

	/* did we get any valid files */
	self->silo = xb_builder_compile (self->builder,
					 XB_BUILDER_COMPILE_FLAG_NONE,
					 NULL, error);
	if (self->silo == NULL)
		return FALSE;

	/* success */
	return TRUE;
}

typedef struct {
	FuCabinet	*self;
	guint64		 size_total;
	GError		*error;
} FuCabinetDecompressHelper;

static gboolean
fu_cabinet_decompress_file_cb (GCabFile *file, gpointer user_data)
{
	FuCabinetDecompressHelper *helper = (FuCabinetDecompressHelper *) user_data;
	FuCabinet *self = FU_CABINET (helper->self);
	g_autofree gchar *basename = NULL;
	g_autofree gchar *name = NULL;

	/* already failed */
	if (helper->error != NULL)
		return FALSE;

	/* check the size of the compressed file */
	if (gcab_file_get_size (file) > self->size_max) {
		g_autofree gchar *sz_val = g_format_size (gcab_file_get_size (file));
		g_autofree gchar *sz_max = g_format_size (self->size_max);
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
	if (helper->size_total > self->size_max) {
		g_autofree gchar *sz_val = g_format_size (helper->size_total);
		g_autofree gchar *sz_max = g_format_size (self->size_max);
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

static gboolean
fu_cabinet_decompress (FuCabinet *self, GBytes *data, GError **error)
{
	FuCabinetDecompressHelper helper = {
		.self		= self,
		.size_total	= 0,
		.error		= NULL,
	};
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GInputStream) istream = NULL;

	/* load from a seekable stream */
	istream = g_memory_input_stream_new_from_bytes (data);
	if (!gcab_cabinet_load (self->gcab_cabinet, istream, NULL, error))
		return FALSE;

	/* check the size is sane */
	if (gcab_cabinet_get_size (self->gcab_cabinet) > self->size_max) {
		g_autofree gchar *sz_val = g_format_size (gcab_cabinet_get_size (self->gcab_cabinet));
		g_autofree gchar *sz_max = g_format_size (self->size_max);
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "archive too large (%s, limit %s)",
			     sz_val, sz_max);
		return FALSE;
	}

	/* decompress the file to memory */
	if (!gcab_cabinet_extract_simple (self->gcab_cabinet, NULL,
					  fu_cabinet_decompress_file_cb, &helper,
					  NULL, &error_local)) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     error_local->message);
		return FALSE;
	}

	/* the file callback set an error */
	if (helper.error != NULL) {
		g_propagate_error (error, helper.error);
		return FALSE;
	}

	/* success */
	return TRUE;
}

/**
 * fu_cabinet_parse:
 * @self: A #FuCabinet
 * @data: A #GBytes
 * @flags: A #FuCabinetParseFlags, e.g. %FU_CABINET_PARSE_FLAG_NONE
 * @error: A #GError, or %NULL
 *
 * Parses the cabinet archive.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.4.0
 **/
gboolean
fu_cabinet_parse (FuCabinet *self,
		  GBytes *data,
		  FuCabinetParseFlags flags,
		  GError **error)
{
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GPtrArray) components = NULL;

	g_return_val_if_fail (FU_IS_CABINET (self), FALSE);
	g_return_val_if_fail (data != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (self->silo == NULL, FALSE);

	/* decompress */
	if (!fu_cabinet_decompress (self, data, error))
		return FALSE;

	/* build xmlb silo */
	self->container_checksum = g_compute_checksum_for_bytes (G_CHECKSUM_SHA1, data);
	if (!fu_cabinet_build_silo (self, data, error))
		return FALSE;

	/* sanity check */
	components = xb_silo_query (self->silo, "components/component", 0, &error_local);
	if (components == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "archive contained no valid metadata: %s",
			     error_local->message);
		return FALSE;
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
			return FALSE;
		}
		for (guint j = 0; j < releases->len; j++) {
			XbNode *rel = g_ptr_array_index (releases, j);
			g_debug ("processing release: %s", xb_node_get_attr (rel, "version"));
			if (!fu_cabinet_parse_release (self, rel, error))
				return FALSE;
		}
	}

	/* success */
	return TRUE;
}

/**
 * fu_cabinet_new:
 *
 * Returns: a #FuCabinet
 *
 * Since: 1.4.0
 **/
FuCabinet *
fu_cabinet_new (void)
{
	return g_object_new (FU_TYPE_CABINET, NULL);
}
