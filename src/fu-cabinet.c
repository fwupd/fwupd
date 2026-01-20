/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuCabinet"

#include "config.h"

#include <fwupdplugin.h>

#include "fu-cabinet.h"

/* fixed in 0.1.14 */
#ifndef JCAT_CHECK_VERSION
#define JCAT_CHECK_VERSION LIBJCAT_CHECK_VERSION
#endif

/**
 * FuCabinet:
 *
 * Cabinet archive parser and writer.
 *
 * See also: [class@FuCabFirmware]
 */

struct _FuCabinet {
	FuCabFirmware parent_instance;
	gchar *container_checksum;
	gchar *container_checksum_alt;
	XbBuilder *builder;
	XbSilo *silo;
	JcatContext *jcat_context;
	JcatFile *jcat_file;
	GHashTable *trustlist; /* basename:FwupdReleaseFlags-as-ptr */
};

G_DEFINE_TYPE(FuCabinet, fu_cabinet, FU_TYPE_CAB_FIRMWARE)

/**
 * fu_cabinet_set_jcat_context: (skip):
 * @self: a #FuCabinet
 * @jcat_context: (nullable): a Jcat context
 *
 * Sets the Jcat context, which is used for setting the trust flags on the
 * each release in the archive.
 *
 * Since: 1.4.0
 **/
void
fu_cabinet_set_jcat_context(FuCabinet *self, JcatContext *jcat_context)
{
	g_return_if_fail(FU_IS_CABINET(self));
	g_return_if_fail(JCAT_IS_CONTEXT(jcat_context));
	g_set_object(&self->jcat_context, jcat_context);
}

/**
 * fu_cabinet_get_silo: (skip):
 * @self: a #FuCabinet
 * @error: (nullable): optional return location for an error
 *
 * Gets the silo that represents the superset metadata of all the metainfo files
 * found in the archive.
 *
 * Returns: (transfer full): a #XbSilo, or %NULL if the archive has not been parsed
 *
 * Since: 1.4.0
 **/
XbSilo *
fu_cabinet_get_silo(FuCabinet *self, GError **error)
{
	g_return_val_if_fail(FU_IS_CABINET(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);
	if (self->silo == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "no silo");
		return NULL;
	}
	return g_object_ref(self->silo);
}

/**
 * fu_cabinet_add_file:
 * @self: a #FuCabinet
 * @basename: filename
 * @data: file data
 * @error: (nullable): optional return location for an error
 *
 * Adds a file to the silo.
 *
 * Returns: %TRUE for success
 **/
gboolean
fu_cabinet_add_file(FuCabinet *self, const gchar *basename, GBytes *data, GError **error)
{
	g_autoptr(FuCabImage) img = fu_cab_image_new();

	g_return_val_if_fail(FU_IS_CABINET(self), FALSE);
	g_return_val_if_fail(basename != NULL, FALSE);
	g_return_val_if_fail(data != NULL, FALSE);

	fu_firmware_set_bytes(FU_FIRMWARE(img), data);
	fu_firmware_set_id(FU_FIRMWARE(img), basename);
	return fu_firmware_add_image(FU_FIRMWARE(self), FU_FIRMWARE(img), error);
}

#if JCAT_CHECK_VERSION(0, 2, 0)
static gboolean
fu_cabinet_verify_payload_target(FuCabinet *self,
				 const gchar *basename,
				 FuFirmware *img_blob,
				 JcatVerifyFlags jcat_flags,
				 GError **error)
{
	g_autofree gchar *checksum_sha256 = NULL;
	g_autofree gchar *checksum_sha512 = NULL;
	g_autoptr(GPtrArray) results = NULL;
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(JcatBlob) blob_target_sha256 = NULL;
	g_autoptr(JcatBlob) blob_target_sha512 = NULL;
	g_autoptr(JcatItem) item = NULL;
	g_autoptr(JcatItem) item_target = jcat_item_new(basename);

	/* get item */
	item = jcat_file_get_item_by_id(self->jcat_file, basename, error);
	if (item == NULL)
		return FALSE;

	/* add SHA-256 */
	stream = fu_firmware_get_stream(img_blob, error);
	if (stream == NULL)
		return FALSE;
	checksum_sha256 = fu_input_stream_compute_checksum(stream, G_CHECKSUM_SHA256, error);
	if (checksum_sha256 == NULL)
		return FALSE;
	blob_target_sha256 = jcat_blob_new_utf8(JCAT_BLOB_KIND_SHA256, checksum_sha256);
	jcat_item_add_blob(item_target, blob_target_sha256);

	/* add SHA-512 */
	checksum_sha512 = fu_input_stream_compute_checksum(stream, G_CHECKSUM_SHA512, error);
	if (checksum_sha512 == NULL)
		return FALSE;
	blob_target_sha512 = jcat_blob_new_utf8(JCAT_BLOB_KIND_SHA512, checksum_sha512);
	jcat_item_add_blob(item_target, blob_target_sha512);

	results = jcat_context_verify_target(self->jcat_context,
					     item_target,
					     item,
					     jcat_flags | JCAT_VERIFY_FLAG_REQUIRE_CHECKSUM |
						 JCAT_VERIFY_FLAG_REQUIRE_SIGNATURE,
					     error);
	if (results == NULL) {
		g_prefix_error_literal(error, "failed to verify indirect item: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}
#endif

static gboolean
fu_cabinet_verify_payload(FuCabinet *self,
			  const gchar *basename,
			  FuFirmware *img_blob,
			  JcatVerifyFlags jcat_flags,
			  GError **error)
{
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GPtrArray) results = NULL;
	g_autoptr(JcatItem) item = NULL;

	/* get item */
	item = jcat_file_get_item_by_id(self->jcat_file, basename, error);
	if (item == NULL)
		return FALSE;

#if JCAT_CHECK_VERSION(0, 2, 0)
	/* the jcat file signed the *checksum of the payload*, not the payload itself */
	if (jcat_item_has_target(item)) {
		return fu_cabinet_verify_payload_target(self,
							basename,
							img_blob,
							jcat_flags,
							error);
	}
#endif

	/* verify the binary item */
	blob = fu_firmware_get_bytes(img_blob, error);
	if (blob == NULL)
		return FALSE;
	results = jcat_context_verify_item(self->jcat_context,
					   blob,
					   item,
					   jcat_flags | JCAT_VERIFY_FLAG_REQUIRE_CHECKSUM |
					       JCAT_VERIFY_FLAG_REQUIRE_SIGNATURE,
					   error);
	if (results == NULL) {
		g_prefix_error_literal(error, "failed to verify item: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_cabinet_trustlist_add_basename(FuCabinet *self,
				  const gchar *basename,
				  FwupdReleaseFlags release_flags)
{
	g_hash_table_insert(self->trustlist, g_strdup(basename), GUINT_TO_POINTER(release_flags));
}

static gboolean
fu_cabinet_trustlist_check_basename(FuCabinet *self,
				    const gchar *basename,
				    FwupdReleaseFlags *release_flags)
{
	gpointer value = NULL;
	if (!g_hash_table_lookup_extended(self->trustlist, basename, NULL, &value))
		return FALSE;
	*release_flags = GPOINTER_TO_UINT(value);
	return TRUE;
}

/* sets the firmware and signature blobs on XbNode */
static gboolean
fu_cabinet_parse_release(FuCabinet *self,
			 XbNode *release,
			 FuFirmwareParseFlags flags,
			 GError **error)
{
	const gchar *csum_filename = NULL;
	gsize streamsz = 0;
	g_autofree gchar *basename = NULL;
	g_autoptr(FuFirmware) img_blob = NULL;
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(GError) error_local2 = NULL;
	g_autoptr(GError) error_payload = NULL;
	g_autoptr(XbNode) artifact = NULL;
	g_autoptr(XbNode) csum_tmp = NULL;
	g_autoptr(XbNode) metadata_trust = NULL;
	g_autoptr(XbNode) nsize = NULL;
	g_autoptr(GBytes) release_flags_blob = NULL;
	g_autoptr(GBytes) filename_blob = NULL;
	FwupdReleaseFlags release_flags = FWUPD_RELEASE_FLAG_NONE;
	FwupdReleaseFlags release_flags_tl = FWUPD_RELEASE_FLAG_NONE;
	JcatVerifyFlags jcat_flags = JCAT_VERIFY_FLAG_DISABLE_TIME_CHECKS;

	/* distrusting RSA? */
	if (flags & FU_FIRMWARE_PARSE_FLAG_ONLY_TRUST_PQ_SIGNATURES) {
#if JCAT_CHECK_VERSION(0, 2, 4)
		jcat_flags |= JCAT_VERIFY_FLAG_ONLY_PQ;
#else
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "only trusting PQ signatures requires libjcat >= 0.2.4");
		return FALSE;
#endif
	}

	/* we set this with XbBuilderSource before the silo was created */
	metadata_trust = xb_node_query_first(release, "../../info/metadata_trust", NULL);
	if (metadata_trust != NULL)
		release_flags |= FWUPD_RELEASE_FLAG_TRUSTED_METADATA;

	/* look for source artifact first */
	artifact = xb_node_query_first(release, "artifacts/artifact[@type='source']", NULL);
	if (artifact != NULL) {
		csum_filename = xb_node_query_text(artifact, "filename", NULL);
		csum_tmp = xb_node_query_first(artifact, "checksum[@type='sha256']", NULL);
		if (csum_tmp == NULL)
			csum_tmp = xb_node_query_first(artifact, "checksum", NULL);
	} else {
		csum_tmp = xb_node_query_first(release, "checksum[@target='content']", NULL);
		if (csum_tmp != NULL)
			csum_filename = xb_node_get_attr(csum_tmp, "filename");
	}

	/* if this isn't true, a firmware needs to set in the metainfo.xml file
	 * something like: <checksum target="content" filename="FLASH.ROM"/> */
	if (csum_filename == NULL)
		csum_filename = "firmware.bin";

	/* get the main firmware file */
	basename = g_path_get_basename(csum_filename);
	img_blob = fu_firmware_get_image_by_id(FU_FIRMWARE(self), basename, &error_local2);
	if (img_blob == NULL) {
		/* we have to set this exact error code */
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    error_local2->message);
		return FALSE;
	}
	filename_blob = g_bytes_new(basename, strlen(basename) + 1);
	xb_node_set_data(release, "fwupd::FirmwareBasename", filename_blob);

	/* set as metadata if unset, but error if specified and incorrect */
	stream = fu_firmware_get_stream(img_blob, error);
	if (stream == NULL)
		return FALSE;
	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	nsize = xb_node_query_first(release, "size[@type='installed']", NULL);
	if (nsize != NULL) {
		guint64 size = 0;
		if (!fu_strtoull(xb_node_get_text(nsize),
				 &size,
				 0,
				 G_MAXSIZE,
				 FU_INTEGER_BASE_AUTO,
				 error))
			return FALSE;
		if (size != streamsz) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "contents size invalid, expected "
				    "%" G_GSIZE_FORMAT ", got %" G_GUINT64_FORMAT,
				    streamsz,
				    size);
			return FALSE;
		}
	} else {
		guint64 size = streamsz;
		g_autoptr(GBytes) blob_sz = g_bytes_new(&size, sizeof(guint64));
		xb_node_set_data(release, "fwupd::ReleaseSize", blob_sz);
	}

	/* set if unspecified, but error out if specified and incorrect */
	if (csum_tmp != NULL && xb_node_get_text(csum_tmp) != NULL) {
		const gchar *checksum_old = xb_node_get_text(csum_tmp);
		GChecksumType checksum_type = fwupd_checksum_guess_kind(checksum_old);
		g_autofree gchar *checksum = NULL;
		checksum = fu_input_stream_compute_checksum(stream, checksum_type, error);
		if (checksum == NULL)
			return FALSE;
		if (g_strcmp0(checksum, checksum_old) != 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "contents checksum invalid, expected %s, got %s",
				    checksum,
				    xb_node_get_text(csum_tmp));
			return FALSE;
		}
	}

	/* is the payload signed? */
	if (fu_cabinet_trustlist_check_basename(self, basename, &release_flags_tl)) {
		g_info("cached verification result for %s", basename);
		release_flags |= (release_flags_tl & FWUPD_RELEASE_FLAG_TRUSTED_PAYLOAD);
	} else {
		if (!fu_cabinet_verify_payload(self,
					       basename,
					       img_blob,
					       jcat_flags,
					       &error_payload)) {
			g_info("failed to verify payload %s: %s", basename, error_payload->message);
		} else {
			g_info("verified payload %s", basename);
			release_flags |= FWUPD_RELEASE_FLAG_TRUSTED_PAYLOAD;
		}
		/* save the trustlist, so that if there are multiple metainfos pointing to the
		 * same payload we only hash and verify once */
		fu_cabinet_trustlist_add_basename(self, basename, release_flags);
	}

	/* this means we can get the data from fu_keyring_get_release_flags */
	release_flags_blob = g_bytes_new(&release_flags, sizeof(release_flags));
	xb_node_set_data(release, "fwupd::ReleaseFlags", release_flags_blob);

	/* success */
	return TRUE;
}

static gint
fu_cabinet_sort_cb(XbBuilderNode *bn1, XbBuilderNode *bn2, gpointer user_data)
{
	guint64 prio1 = xb_builder_node_get_attr_as_uint(bn1, "priority");
	guint64 prio2 = xb_builder_node_get_attr_as_uint(bn2, "priority");
	if (prio1 > prio2)
		return -1;
	if (prio1 < prio2)
		return 1;
	return 0;
}

static gboolean
fu_cabinet_sort_priority_cb(XbBuilderFixup *self,
			    XbBuilderNode *bn,
			    gpointer user_data,
			    GError **error)
{
	xb_builder_node_sort_children(bn, fu_cabinet_sort_cb, user_data);
	return TRUE;
}

static XbBuilderNode *
_xb_builder_node_get_child_by_element_attr(XbBuilderNode *bn,
					   const gchar *element,
					   const gchar *attr_name,
					   const gchar *attr_value,
					   const gchar *attr2_name,
					   const gchar *attr2_value)
{
	GPtrArray *bcs = xb_builder_node_get_children(bn);
	for (guint i = 0; i < bcs->len; i++) {
		XbBuilderNode *bc = g_ptr_array_index(bcs, i);
		if (g_strcmp0(xb_builder_node_get_element(bc), element) != 0)
			continue;
		if (g_strcmp0(xb_builder_node_get_attr(bc, attr_name), attr_value) != 0)
			continue;
		if (g_strcmp0(xb_builder_node_get_attr(bc, attr2_name), attr2_value) == 0)
			return g_object_ref(bc);
	}
	return NULL;
}

static void
fu_cabinet_ensure_container_checksum(XbBuilderNode *bn, const gchar *type, const gchar *checksum)
{
	g_autoptr(XbBuilderNode) csum = NULL;

	/* verify it exists */
	csum = _xb_builder_node_get_child_by_element_attr(bn,
							  "checksum",
							  "type",
							  type,
							  "target",
							  "container");
	if (csum == NULL) {
		csum = xb_builder_node_insert(bn,
					      "checksum",
					      "type",
					      type,
					      "target",
					      "container",
					      NULL);
	}

	/* verify it is correct */
	if (g_strcmp0(xb_builder_node_get_text(csum), checksum) != 0) {
		if (xb_builder_node_get_text(csum) != NULL) {
			g_warning("invalid container checksum %s, fixing up to %s",
				  xb_builder_node_get_text(csum),
				  checksum);
		}
		xb_builder_node_set_text(csum, checksum, -1);
	}
}

static gboolean
fu_cabinet_ensure_container_checksum_cb(XbBuilderFixup *builder_fixup,
					XbBuilderNode *bn,
					gpointer user_data,
					GError **error)
{
	FuCabinet *self = FU_CABINET(user_data);

	/* not us */
	if (g_strcmp0(xb_builder_node_get_element(bn), "release") != 0)
		return TRUE;

	fu_cabinet_ensure_container_checksum(bn, "sha1", self->container_checksum);
	fu_cabinet_ensure_container_checksum(bn, "sha256", self->container_checksum_alt);
	return TRUE;
}

static void
fu_cabinet_fixup_checksum_children(XbBuilderNode *bn,
				   const gchar *element,
				   const gchar *attr_name,
				   const gchar *attr_value)
{
	GPtrArray *bcs = xb_builder_node_get_children(bn);
	for (guint i = 0; i < bcs->len; i++) {
		XbBuilderNode *bc = g_ptr_array_index(bcs, i);
		if (g_strcmp0(xb_builder_node_get_element(bc), element) != 0)
			continue;
		if (attr_value == NULL ||
		    g_strcmp0(xb_builder_node_get_attr(bc, attr_name), attr_value) == 0) {
			const gchar *tmp = xb_builder_node_get_text(bc);
			if (tmp != NULL) {
				g_autofree gchar *lowercase = g_ascii_strdown(tmp, -1);
				xb_builder_node_set_text(bc, lowercase, -1);
			}
		}
	}
}

static gboolean
fu_cabinet_set_lowercase_checksum_cb(XbBuilderFixup *builder_fixup,
				     XbBuilderNode *bn,
				     gpointer user_data,
				     GError **error)
{
	if (g_strcmp0(xb_builder_node_get_element(bn), "artifact") == 0)
		/* don't care whether it's sha256, sha1 or something else so don't check for
		 * specific value */
		fu_cabinet_fixup_checksum_children(bn, "checksum", "type", NULL);
	else if (g_strcmp0(xb_builder_node_get_element(bn), "release") == 0)
		fu_cabinet_fixup_checksum_children(bn, "checksum", "target", "content");

	return TRUE;
}

static gboolean
fu_cabinet_fixup_strip_inner_text_cb(XbBuilderFixup *self,
				     XbBuilderNode *bn,
				     gpointer user_data,
				     GError **error)
{
	if (xb_builder_node_get_first_child(bn) == NULL)
		xb_builder_node_add_flag(bn, XB_BUILDER_NODE_FLAG_STRIP_TEXT);
	return TRUE;
}

/* adds each image to the silo */
static gboolean
fu_cabinet_build_silo_file(FuCabinet *self,
			   FuFirmware *img,
			   FwupdReleaseFlags release_flags,
			   GError **error)
{
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(XbBuilderSource) source = xb_builder_source_new();
	g_autoptr(XbBuilderNode) bn_info = xb_builder_node_new("info");

	/* indicate the metainfo file was signed */
	if (release_flags & FWUPD_RELEASE_FLAG_TRUSTED_METADATA)
		xb_builder_node_insert_text(bn_info, "metadata_trust", NULL, NULL);
	xb_builder_node_insert_text(bn_info, "filename", fu_firmware_get_id(img), NULL);
	xb_builder_source_set_info(source, bn_info);

	/* rewrite to be under a components root */
	xb_builder_source_set_prefix(source, "components");

	/* parse file */
	blob = fu_firmware_get_bytes(img, error);
	if (blob == NULL)
		return FALSE;
	if (!xb_builder_source_load_bytes(source,
					  blob,
					  XB_BUILDER_SOURCE_FLAG_NONE,
					  &error_local)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "could not parse MetaInfo XML: %s",
			    error_local->message);
		return FALSE;
	}
	xb_builder_import_source(self->builder, source);

	/* success */
	return TRUE;
}

static gboolean
fu_cabinet_build_silo_metainfo(FuCabinet *self,
			       FuFirmware *img,
			       FuFirmwareParseFlags flags,
			       GError **error)
{
	FwupdReleaseFlags release_flags = FWUPD_RELEASE_FLAG_NONE;
	const gchar *fn = fu_firmware_get_id(img);
	g_autoptr(JcatItem) item = NULL;
	JcatVerifyFlags jcat_flags =
	    JCAT_VERIFY_FLAG_REQUIRE_CHECKSUM | JCAT_VERIFY_FLAG_REQUIRE_SIGNATURE;

	/* distrusting RSA? */
	if (flags & FU_FIRMWARE_PARSE_FLAG_ONLY_TRUST_PQ_SIGNATURES) {
#if JCAT_CHECK_VERSION(0, 2, 4)
		jcat_flags |= JCAT_VERIFY_FLAG_ONLY_PQ;
#else
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "only trusting PQ signatures requires libjcat >= 0.2.4");
		return FALSE;
#endif
	}

	/* validate against the Jcat file */
	item = jcat_file_get_item_by_id(self->jcat_file, fn, NULL);
	if (item == NULL) {
		g_info("failed to verify %s: no JcatItem", fn);
	} else if (self->jcat_context != NULL) {
		g_autoptr(GError) error_local = NULL;
		g_autoptr(GPtrArray) results = NULL;
		g_autoptr(GBytes) blob = NULL;

		blob = fu_firmware_get_bytes(img, error);
		if (blob == NULL)
			return FALSE;
		results = jcat_context_verify_item(self->jcat_context,
						   blob,
						   item,
						   jcat_flags,
						   &error_local);
		if (results == NULL) {
			g_info("failed to verify %s: %s", fn, error_local->message);
		} else {
			g_info("verified metadata %s: %u", fn, results->len);
			release_flags |= FWUPD_RELEASE_FLAG_TRUSTED_METADATA;
		}
	}

	/* actually parse the XML now */
	g_info("processing file: %s", fn);
	if (!fu_cabinet_build_silo_file(self, img, release_flags, error)) {
		g_prefix_error(error, "%s could not be loaded: ", fn);
		return FALSE;
	}

	/* success */
	return TRUE;
}

/* load the firmware.jcat files if included */
static gboolean
fu_cabinet_build_jcat_folder(FuCabinet *self, FuFirmware *img, GError **error)
{
	const gchar *fn = fu_firmware_get_id(img);
	if (fn == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "no extraction name set");
		return FALSE;
	}
	if (g_str_has_suffix(fn, ".jcat")) {
		g_autoptr(GInputStream) istream = NULL;
		istream = fu_firmware_get_stream(img, error);
		if (istream == NULL)
			return FALSE;
		/* TODO: move this to libjcat? */
		if (!g_seekable_seek(G_SEEKABLE(istream), 0x0, G_SEEK_SET, NULL, error))
			return FALSE;
		if (!jcat_file_import_stream(self->jcat_file,
					     istream,
					     JCAT_IMPORT_FLAG_NONE,
					     NULL,
					     error)) {
			g_prefix_error_literal(error, "failed to import JCat stream: ");
			return FALSE;
		}
	}
	return TRUE;
}

/* adds each image to the silo */
static gboolean
fu_cabinet_build_silo_folder(FuCabinet *self,
			     FuFirmware *img,
			     FuFirmwareParseFlags flags,
			     GError **error)
{
	const gchar *fn = fu_firmware_get_id(img);
	if (fn == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "no extraction name set");
		return FALSE;
	}
	if (g_str_has_suffix(fn, ".metainfo.xml")) {
		if (!fu_cabinet_build_silo_metainfo(self, img, flags, error))
			return FALSE;
	}
	return TRUE;
}

static gboolean
fu_cabinet_build_silo(FuCabinet *self, FuFirmwareParseFlags flags, GError **error)
{
	g_autoptr(GPtrArray) imgs = NULL;
	g_autoptr(XbBuilderFixup) fixup1 = NULL;
	g_autoptr(XbBuilderFixup) fixup2 = NULL;
	g_autoptr(XbBuilderFixup) fixup3 = NULL;
	g_autoptr(XbBuilderFixup) fixup4 = NULL;
	g_autoptr(XbNode) guid1 = NULL;

	/* verbose profiling */
	if (g_getenv("FWUPD_XMLB_VERBOSE") != NULL) {
		xb_builder_set_profile_flags(self->builder,
					     XB_SILO_PROFILE_FLAG_XPATH |
						 XB_SILO_PROFILE_FLAG_DEBUG);
	}

	/* load Jcat */
	imgs = fu_firmware_get_images(FU_FIRMWARE(FU_CAB_FIRMWARE(self)));
	if (self->jcat_context != NULL) {
		for (guint i = 0; i < imgs->len; i++) {
			FuFirmware *img = g_ptr_array_index(imgs, i);
			if (!fu_cabinet_build_jcat_folder(self, img, error))
				return FALSE;
		}
	}

	/* adds each metainfo file to the silo */
	for (guint i = 0; i < imgs->len; i++) {
		FuFirmware *img = g_ptr_array_index(imgs, i);
		if (!fu_cabinet_build_silo_folder(self, img, flags, error))
			return FALSE;
	}

	/* sort the components by priority */
	fixup1 = xb_builder_fixup_new("OrderByPriority", fu_cabinet_sort_priority_cb, NULL, NULL);
	xb_builder_fixup_set_max_depth(fixup1, 0);
	xb_builder_add_fixup(self->builder, fixup1);

	/* ensure the container checksum is always set */
	fixup2 = xb_builder_fixup_new("EnsureContainerChecksum",
				      fu_cabinet_ensure_container_checksum_cb,
				      self,
				      NULL);
	xb_builder_add_fixup(self->builder, fixup2);

	fixup3 = xb_builder_fixup_new("LowerCaseCheckSum",
				      fu_cabinet_set_lowercase_checksum_cb,
				      self,
				      NULL);
	xb_builder_add_fixup(self->builder, fixup3);

	/* strip inner nodes without children */
	fixup4 = xb_builder_fixup_new("TextStripInner",
				      fu_cabinet_fixup_strip_inner_text_cb,
				      self,
				      NULL);
	xb_builder_add_fixup(self->builder, fixup4);

	/* did we get any valid files */
	self->silo =
	    xb_builder_compile(self->builder, XB_BUILDER_COMPILE_FLAG_SINGLE_ROOT, NULL, error);
	if (self->silo == NULL) {
		fwupd_error_convert(error);
		return FALSE;
	}

	/* verify there's at least one GUID */
	guid1 = xb_silo_query_first(self->silo,
				    "components/component[@type='firmware']/"
				    "provides/firmware[@type='flashed']",
				    error);
	if (guid1 == NULL) {
		fwupd_error_convert(error);
		return FALSE;
	}
	if (xb_node_get_text(guid1) == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "no <firmware type='flashed'> data");
		return FALSE;
	}

	/* build the index */
	if (!xb_silo_query_build_index(self->silo,
				       "components/component[@type='firmware']/provides/firmware",
				       "type",
				       error)) {
		fwupd_error_convert(error);
		return FALSE;
	}
	if (!xb_silo_query_build_index(self->silo,
				       "components/component[@type='firmware']/provides/firmware",
				       NULL,
				       error)) {
		fwupd_error_convert(error);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_cabinet_sign_filename(FuCabinet *self,
			 const gchar *filename,
			 JcatContext *jcat_context,
			 JcatFile *jcat_file,
			 GBytes *cert,
			 GBytes *privkey,
			 GError **error)
{
	g_autoptr(FuFirmware) img = NULL;
	g_autoptr(GBytes) source_blob = NULL;
	g_autoptr(JcatBlob) jcat_blob_csum = NULL;
	g_autoptr(JcatBlob) jcat_blob_sig = NULL;
	g_autoptr(JcatEngine) jcat_engine_csum = NULL;
	g_autoptr(JcatEngine) jcat_engine_sig = NULL;
	g_autoptr(JcatItem) jcat_item = NULL;

	/* sign the file using the engine */
	img = fu_firmware_get_image_by_id(FU_FIRMWARE(self), filename, error);
	if (img == NULL)
		return FALSE;
	source_blob = fu_firmware_get_bytes(img, error);
	if (source_blob == NULL)
		return FALSE;
	jcat_item = jcat_file_get_item_by_id(jcat_file, filename, NULL);
	if (jcat_item == NULL) {
		jcat_item = jcat_item_new(filename);
		jcat_file_add_item(jcat_file, jcat_item);
	}

	/* add SHA256 checksum */
	jcat_engine_csum = jcat_context_get_engine(jcat_context, JCAT_BLOB_KIND_SHA256, error);
	if (jcat_engine_csum == NULL)
		return FALSE;
	jcat_blob_csum =
	    jcat_engine_self_sign(jcat_engine_csum, source_blob, JCAT_SIGN_FLAG_NONE, error);
	if (jcat_blob_csum == NULL)
		return FALSE;
	jcat_item_add_blob(jcat_item, jcat_blob_csum);

	/* sign using PKCS#7 */
	jcat_engine_sig = jcat_context_get_engine(jcat_context, JCAT_BLOB_KIND_PKCS7, error);
	if (jcat_engine_sig == NULL)
		return FALSE;
	jcat_blob_sig =
	    jcat_engine_pubkey_sign(jcat_engine_sig,
				    source_blob,
				    cert,
				    privkey,
				    JCAT_SIGN_FLAG_ADD_TIMESTAMP | JCAT_SIGN_FLAG_ADD_CERT,
				    error);
	if (jcat_blob_sig == NULL)
		return FALSE;
	jcat_item_add_blob(jcat_item, jcat_blob_sig);
	return TRUE;
}

static gboolean
fu_cabinet_sign_enumerate_metainfo(FuCabinet *self, GPtrArray *files, GError **error)
{
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GPtrArray) nodes = NULL;
	g_autoptr(XbSilo) silo = NULL;

	/* get all the firmware referenced by the metainfo files */
	silo = fu_cabinet_get_silo(self, error);
	if (silo == NULL)
		return FALSE;
	nodes = xb_silo_query(silo,
			      "components/component[@type='firmware']/info/filename",
			      0,
			      &error_local);
	if (nodes == NULL) {
		if (g_error_matches(error_local, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT) ||
		    g_error_matches(error_local, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
			g_debug("ignoring: %s", error_local->message);
			g_ptr_array_add(files, g_strdup("firmware.metainfo.xml"));
		} else {
			g_propagate_error(error, g_steal_pointer(&error_local));
			fwupd_error_convert(error);
			return FALSE;
		}
	} else {
		for (guint i = 0; i < nodes->len; i++) {
			XbNode *n = g_ptr_array_index(nodes, i);
			g_debug("adding: %s", xb_node_get_text(n));
			g_ptr_array_add(files, g_strdup(xb_node_get_text(n)));
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_cabinet_sign_enumerate_firmware(FuCabinet *self, GPtrArray *files, GError **error)
{
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GPtrArray) nodes = NULL;
	g_autoptr(XbSilo) silo = NULL;

	silo = fu_cabinet_get_silo(self, error);
	if (silo == NULL)
		return FALSE;
	nodes = xb_silo_query(silo,
			      "components/component[@type='firmware']/releases/"
			      "release/checksum[@target='content']",
			      0,
			      &error_local);
	if (nodes == NULL) {
		if (g_error_matches(error_local, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT) ||
		    g_error_matches(error_local, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
			g_debug("ignoring: %s", error_local->message);
			g_ptr_array_add(files, g_strdup("firmware.bin"));
		} else {
			g_propagate_error(error, g_steal_pointer(&error_local));
			fwupd_error_convert(error);
			return FALSE;
		}
	} else {
		for (guint i = 0; i < nodes->len; i++) {
			XbNode *n = g_ptr_array_index(nodes, i);
			g_debug("adding: %s", xb_node_get_attr(n, "filename"));
			g_ptr_array_add(files, g_strdup(xb_node_get_attr(n, "filename")));
		}
	}

	/* success */
	return TRUE;
}

/**
 * fu_cabinet_sign:
 * @self: a #FuCabinet
 * @cert: a PCKS#7 certificate
 * @privkey: a private key
 * @flags: signing flags, e.g. %FU_CABINET_SIGN_FLAG_NONE
 * @error: (nullable): optional return location for an error
 *
 * Sign the cabinet archive using JCat.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.6.0
 **/
gboolean
fu_cabinet_sign(FuCabinet *self,
		GBytes *cert,
		GBytes *privkey,
		FuCabinetSignFlags flags,
		GError **error)
{
	g_autoptr(FuFirmware) img = NULL;
	g_autoptr(GBytes) new_bytes = NULL;
	g_autoptr(GOutputStream) ostr = NULL;
	g_autoptr(GPtrArray) filenames = g_ptr_array_new_with_free_func(g_free);
	g_autoptr(JcatContext) jcat_context = jcat_context_new();
	g_autoptr(JcatFile) jcat_file = jcat_file_new();

	g_return_val_if_fail(FU_IS_CABINET(self), FALSE);
	g_return_val_if_fail(cert != NULL, FALSE);
	g_return_val_if_fail(privkey != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* load existing .jcat file if it exists */
	img = fu_firmware_get_image_by_id(FU_FIRMWARE(self), "firmware.jcat", NULL);
	if (img != NULL) {
		g_autoptr(GInputStream) stream = fu_firmware_get_stream(img, error);
		if (stream == NULL)
			return FALSE;
		if (!jcat_file_import_stream(jcat_file, stream, JCAT_IMPORT_FLAG_NONE, NULL, error))
			return FALSE;
	}

	/* get all the metainfo.xml and firmware.bin files */
	if (!fu_cabinet_sign_enumerate_metainfo(self, filenames, error))
		return FALSE;
	if (!fu_cabinet_sign_enumerate_firmware(self, filenames, error))
		return FALSE;

	/* sign all the files */
	for (guint i = 0; i < filenames->len; i++) {
		const gchar *filename = g_ptr_array_index(filenames, i);
		if (!fu_cabinet_sign_filename(self,
					      filename,
					      jcat_context,
					      jcat_file,
					      cert,
					      privkey,
					      error))
			return FALSE;
	}

	/* export new JCat file and add it to the archive */
	ostr = g_memory_output_stream_new_resizable();
	if (!jcat_file_export_stream(jcat_file, ostr, JCAT_EXPORT_FLAG_NONE, NULL, error))
		return FALSE;
	new_bytes = g_memory_output_stream_steal_as_bytes(G_MEMORY_OUTPUT_STREAM(ostr));
	return fu_cabinet_add_file(self, "firmware.jcat", new_bytes, error);
}

static gboolean
fu_cabinet_parse(FuFirmware *firmware,
		 GInputStream *stream,
		 FuFirmwareParseFlags flags,
		 GError **error)
{
	FuCabinet *self = FU_CABINET(firmware);
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GPtrArray) components = NULL;
	g_autoptr(XbQuery) query = NULL;

	g_return_val_if_fail(FU_IS_CABINET(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail(self->silo == NULL, FALSE);

	/* decompress and calculate container hashes */
	if (stream != NULL) {
		if ((flags & FU_FIRMWARE_PARSE_FLAG_CACHE_STREAM) == 0 &&
		    (flags & FU_FIRMWARE_PARSE_FLAG_CACHE_BLOB) == 0) {
			g_set_error_literal(
			    error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "FuCabinet requires FU_FIRMWARE_PARSE_FLAG_CACHE_STREAM or "
			    "FU_FIRMWARE_PARSE_FLAG_CACHE_BLOB for accurate checksums");
			return FALSE;
		}
		if (!FU_FIRMWARE_CLASS(fu_cabinet_parent_class)
			 ->parse(firmware,
				 stream,
				 flags | FU_FIRMWARE_PARSE_FLAG_ONLY_BASENAME,
				 error))
			return FALSE;
		self->container_checksum =
		    fu_input_stream_compute_checksum(stream, G_CHECKSUM_SHA1, error);
		if (self->container_checksum == NULL)
			return FALSE;
		self->container_checksum_alt =
		    fu_input_stream_compute_checksum(stream, G_CHECKSUM_SHA256, error);
		if (self->container_checksum_alt == NULL)
			return FALSE;
	}

	/* build xmlb silo */
	if (!fu_cabinet_build_silo(self, flags, error))
		return FALSE;

	/* sanity check */
	components = xb_silo_query(self->silo, "components/component", 0, &error_local);
	if (components == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "archive contained no valid metadata: %s",
			    error_local->message);
		return FALSE;
	}

	/* prepare query */
	query = xb_query_new_full(self->silo,
				  "releases/release",
				  XB_QUERY_FLAG_FORCE_NODE_CACHE,
				  error);
	if (query == NULL)
		return FALSE;

	/* process each listed release */
	for (guint i = 0; i < components->len; i++) {
		XbNode *component = g_ptr_array_index(components, i);
		g_autoptr(GPtrArray) releases = NULL;
		if (g_strcmp0(xb_node_get_attr(component, "type"), "generic") == 0)
			continue;
		releases = xb_node_query_full(component, query, &error_local);
		if (releases == NULL) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "no releases in metainfo file: %s",
				    error_local->message);
			return FALSE;
		}
		for (guint j = 0; j < releases->len; j++) {
			XbNode *rel = g_ptr_array_index(releases, j);
			g_info("processing release: %s", xb_node_get_attr(rel, "version"));
			if (!fu_cabinet_parse_release(self, rel, flags, error))
				return FALSE;
		}
	}

	/* success */
	return TRUE;
}

GPtrArray *
fu_cabinet_get_components(FuCabinet *self, GError **error)
{
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GPtrArray) components = NULL;

	g_return_val_if_fail(FU_IS_CABINET(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	components =
	    xb_silo_query(self->silo, "components/component[@type='firmware']", 0, &error_local);
	if (components == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "no components: %s",
			    error_local->message);
		return NULL;
	}
	return g_steal_pointer(&components);
}

XbNode *
fu_cabinet_get_component(FuCabinet *self, const gchar *id, GError **error)
{
	g_autofree gchar *xpath = NULL;
	g_autoptr(XbNode) xn = NULL;

	g_return_val_if_fail(FU_IS_CABINET(self), NULL);
	g_return_val_if_fail(id != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	xpath = g_strdup_printf("components/component/id[text()='%s']/..", id);
	xn = xb_silo_query_first(self->silo, xpath, error);
	if (xn == NULL) {
		fwupd_error_convert(error);
		return NULL;
	}
	return g_steal_pointer(&xn);
}

static void
fu_cabinet_init(FuCabinet *self)
{
	fu_firmware_set_size_max(FU_FIRMWARE(self), G_MAXUINT32); /* ~4GB */
	self->builder = xb_builder_new();
	self->jcat_file = jcat_file_new();
	self->jcat_context = jcat_context_new();
	self->trustlist = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
#if JCAT_CHECK_VERSION(0, 1, 13)
	jcat_context_blob_kind_allow(self->jcat_context, JCAT_BLOB_KIND_SHA256);
	jcat_context_blob_kind_allow(self->jcat_context, JCAT_BLOB_KIND_SHA512);
	jcat_context_blob_kind_allow(self->jcat_context, JCAT_BLOB_KIND_PKCS7);
	jcat_context_blob_kind_allow(self->jcat_context, JCAT_BLOB_KIND_GPG);
#endif
}

static void
fu_cabinet_finalize(GObject *obj)
{
	FuCabinet *self = FU_CABINET(obj);
	if (self->silo != NULL)
		g_object_unref(self->silo);
	if (self->builder != NULL)
		g_object_unref(self->builder);
	g_free(self->container_checksum);
	g_free(self->container_checksum_alt);
	g_hash_table_unref(self->trustlist);
	g_object_unref(self->jcat_context);
	g_object_unref(self->jcat_file);
	G_OBJECT_CLASS(fu_cabinet_parent_class)->finalize(obj);
}

static void
fu_cabinet_class_init(FuCabinetClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	object_class->finalize = fu_cabinet_finalize;
	firmware_class->parse = fu_cabinet_parse;
}

/**
 * fu_cabinet_new:
 *
 * Returns: a #FuCabinet
 *
 * Since: 1.4.0
 **/
FuCabinet *
fu_cabinet_new(void)
{
	return g_object_new(FU_TYPE_CABINET, NULL);
}
