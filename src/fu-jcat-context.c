/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fwupd-jcat-blob.h"

#include "fu-jcat-context.h"
#include "fu-jcat-engine.h"
#include "fu-jcat-result.h"
#include "fu-jcat-sha256-engine.h"
#include "fu-jcat-sha512-engine.h"

#ifdef HAVE_GNUTLS
#include "fu-jcat-gnutls-pkcs7-engine.h"
#endif

struct _FuJcatContext {
	GObject parent_instance;
	GPtrArray *engines;
	GPtrArray *public_keys;
	gchar *keyring_path;
	guint32 blob_kinds;
};

G_DEFINE_TYPE(FuJcatContext, fu_jcat_context, G_TYPE_OBJECT)

static void
fu_jcat_context_finalize(GObject *obj)
{
	FuJcatContext *self = FU_JCAT_CONTEXT(obj);
	g_free(self->keyring_path);
	g_ptr_array_unref(self->engines);
	g_ptr_array_unref(self->public_keys);
	G_OBJECT_CLASS(fu_jcat_context_parent_class)->finalize(obj);
}

static void
fu_jcat_context_class_init(FuJcatContextClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_jcat_context_finalize;
}

static void
fu_jcat_context_init(FuJcatContext *self)
{
	self->keyring_path = g_build_filename(g_get_user_data_dir(), PACKAGE_NAME, NULL);
	self->engines = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	self->public_keys = g_ptr_array_new_with_free_func(g_free);

	g_ptr_array_add(self->engines, fu_jcat_sha256_engine_new(self));
	g_ptr_array_add(self->engines, fu_jcat_sha512_engine_new(self));
#ifdef HAVE_GNUTLS
	g_ptr_array_add(self->engines, fu_jcat_gnutls_pkcs7_engine_new(self));
#endif
}

/**
 * fu_jcat_context_add_public_keys:
 * @self: #FuJcatContext
 * @path: A directory of files
 *
 * Adds a public key directory.
 **/
void
fu_jcat_context_add_public_keys(FuJcatContext *self, const gchar *path)
{
	const gchar *fn_tmp;
	g_autoptr(GDir) dir = NULL;
	g_autoptr(GError) error_local = NULL;

	g_return_if_fail(FU_IS_JCAT_CONTEXT(self));
	g_return_if_fail(path != NULL);

	/* search all the public key files */
	dir = g_dir_open(path, 0, &error_local);
	if (dir == NULL) {
		g_debug("failed to open public keys directory %s: %s", path, error_local->message);
		return;
	}
	while ((fn_tmp = g_dir_read_name(dir)) != NULL)
		g_ptr_array_add(self->public_keys, g_build_filename(path, fn_tmp, NULL));
}

/* private */
GPtrArray *
fu_jcat_context_get_public_keys(FuJcatContext *self)
{
	return self->public_keys;
}

/**
 * fu_jcat_context_set_keyring_path:
 * @self: #FuJcatContext
 * @path: A directory
 *
 * Sets the local state directory for the engines to use.
 **/
void
fu_jcat_context_set_keyring_path(FuJcatContext *self, const gchar *path)
{
	g_return_if_fail(FU_IS_JCAT_CONTEXT(self));
	g_return_if_fail(path != NULL);
	g_free(self->keyring_path);
	self->keyring_path = g_strdup(path);
}

/**
 * fu_jcat_context_get_keyring_path:
 * @self: #FuJcatContext
 *
 * Gets the local state directory the engines are using.
 *
 * Returns: (nullable): path
 **/
const gchar *
fu_jcat_context_get_keyring_path(FuJcatContext *self)
{
	g_return_val_if_fail(FU_IS_JCAT_CONTEXT(self), NULL);
	return self->keyring_path;
}

static gboolean
fu_jcat_context_is_blob_kind_allowed(FuJcatContext *self, FwupdJcatBlobKind kind)
{
	if (kind >= FWUPD_JCAT_BLOB_KIND_LAST)
		return FALSE;
	return (self->blob_kinds & (1u << kind)) > 0;
}

/**
 * fu_jcat_context_get_engine:
 * @self: #FuJcatContext
 * @kind: #FwupdJcatBlobKind, e.g. %FWUPD_JCAT_BLOB_KIND_GPG
 * @error: #GError, or %NULL
 *
 * Gets the engine for a specific engine kind, setting up the context
 * automatically if required.
 *
 * Returns: (transfer full): #FuJcatEngine, or %NULL for unavailable
 **/
FuJcatEngine *
fu_jcat_context_get_engine(FuJcatContext *self, FwupdJcatBlobKind kind, GError **error)
{
	g_return_val_if_fail(FU_IS_JCAT_CONTEXT(self), NULL);

	if (!fu_jcat_context_is_blob_kind_allowed(self, kind)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "FwupdJcat engine kind '%s' not allowed",
			    fwupd_jcat_blob_kind_to_string(kind));
		return NULL;
	}
	for (guint i = 0; i < self->engines->len; i++) {
		FuJcatEngine *engine = g_ptr_array_index(self->engines, i);
		if (fu_jcat_engine_get_kind(engine) == kind)
			return g_object_ref(engine);
	}
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_FOUND,
		    "FwupdJcat engine kind '%s' not supported",
		    fwupd_jcat_blob_kind_to_string(kind));
	return NULL;
}

/**
 * fu_jcat_context_verify_blob:
 * @self: #FuJcatContext
 * @data: #GBytes
 * @blob: #FwupdJcatBlob
 * @flags: #FuJcatVerifyFlags, e.g. %FU_JCAT_VERIFY_FLAG_DISABLE_TIME_CHECKS
 * @error: #GError, or %NULL
 *
 * Verifies a #FwupdJcatBlob using the public keys added to the context.
 *
 * Returns: (transfer full): #FuJcatResult, or %NULL for failed
 **/
FuJcatResult *
fu_jcat_context_verify_blob(FuJcatContext *self,
			    GBytes *data,
			    FwupdJcatBlob *blob,
			    FuJcatVerifyFlags flags,
			    GError **error)
{
	GBytes *blob_signature;
	g_autoptr(FuJcatEngine) engine = NULL;

	g_return_val_if_fail(FU_IS_JCAT_CONTEXT(self), NULL);
	g_return_val_if_fail(data != NULL, NULL);
	g_return_val_if_fail(FWUPD_IS_JCAT_BLOB(blob), NULL);

	/* get correct engine */
	engine = fu_jcat_context_get_engine(self, fwupd_jcat_blob_get_kind(blob), error);
	if (engine == NULL)
		return NULL;
	blob_signature = fwupd_jcat_blob_get_data(blob);
	if (blob_signature == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "blob has no signature data");
		return NULL;
	}
	if (fu_jcat_engine_get_method(engine) == FWUPD_JCAT_BLOB_METHOD_CHECKSUM)
		return fu_jcat_engine_self_verify(engine, data, blob_signature, flags, error);
	return fu_jcat_engine_pubkey_verify(engine, data, blob_signature, flags, error);
}

/**
 * fu_jcat_context_allow_blob_kind:
 * @self: #FuJcatContext
 * @kind: #FwupdJcatBlobKind, e.g. %FWUPD_JCAT_BLOB_KIND_GPG
 *
 * Adds a blob kind to the allowlist. By default, JCat will trust no blob kinds.
 *
 * Once this function has been called only specific blob kinds will be used in functions like
 * fu_jcat_context_verify_blob().
 **/
void
fu_jcat_context_allow_blob_kind(FuJcatContext *self, FwupdJcatBlobKind kind)
{
	g_return_if_fail(FU_IS_JCAT_CONTEXT(self));
	g_return_if_fail(kind < FWUPD_JCAT_BLOB_KIND_LAST);
	FU_BIT_SET(self->blob_kinds, kind);
}

/**
 * fu_jcat_context_verify_item:
 * @self: #FuJcatContext
 * @data: #GBytes
 * @item: #FwupdJcatItem
 * @flags: #FuJcatVerifyFlags, e.g. %FU_JCAT_VERIFY_FLAG_REQUIRE_SIGNATURE
 * @error: #GError, or %NULL
 *
 * Verifies a #FwupdJcatItem using the public keys added to the context. All
 * `verify=CHECKSUM` engines (e.g. SHA256) must verify correctly,
 * but only one non-checksum signature has to verify.
 *
 * Returns: (transfer container) (element-type FuJcatResult): array of #FuJcatResult, or %NULL for
 * failed
 **/
GPtrArray *
fu_jcat_context_verify_item(FuJcatContext *self,
			    GBytes *data,
			    FwupdJcatItem *item,
			    FuJcatVerifyFlags flags,
			    GError **error)
{
	guint nr_signature = 0;
	g_autoptr(GPtrArray) blobs = NULL;
	g_autoptr(GPtrArray) results =
	    g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);

	g_return_val_if_fail(FU_IS_JCAT_CONTEXT(self), NULL);
	g_return_val_if_fail(data != NULL, NULL);
	g_return_val_if_fail(FWUPD_IS_JCAT_ITEM(item), NULL);

	/* no blobs */
	blobs = fwupd_jcat_item_get_blobs(item);
	if (blobs->len == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no blobs in item");
		return NULL;
	}

	/* all checksum engines must verify */
	for (guint i = 0; i < blobs->len; i++) {
		FwupdJcatBlob *blob = g_ptr_array_index(blobs, i);
		g_autoptr(GError) error_local = NULL;
		g_autoptr(FuJcatEngine) engine = NULL;
		g_autoptr(FuJcatResult) result = NULL;

		/* get engine */
		engine =
		    fu_jcat_context_get_engine(self, fwupd_jcat_blob_get_kind(blob), &error_local);
		if (engine == NULL) {
			g_debug("%s", error_local->message);
			continue;
		}
		if (fu_jcat_engine_get_method(engine) != FWUPD_JCAT_BLOB_METHOD_CHECKSUM)
			continue;
		result = fu_jcat_engine_self_verify(engine,
						    data,
						    fwupd_jcat_blob_get_data(blob),
						    flags,
						    error);
		if (result == NULL) {
			g_prefix_error_literal(error, "checksum failure: ");
			return NULL;
		}
		g_ptr_array_add(results, g_steal_pointer(&result));
	}
	if (flags & FU_JCAT_VERIFY_FLAG_REQUIRE_CHECKSUM && results->len == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "checksums were required, but none supplied");
		return NULL;
	}

	/* we only have to have one non-checksum method to verify */
	for (guint i = 0; i < blobs->len; i++) {
		FwupdJcatBlob *blob = g_ptr_array_index(blobs, i);
		g_autofree gchar *result_str = NULL;
		g_autoptr(GError) error_local = NULL;
		g_autoptr(FuJcatEngine) engine = NULL;
		g_autoptr(FuJcatResult) result = NULL;

		engine =
		    fu_jcat_context_get_engine(self, fwupd_jcat_blob_get_kind(blob), &error_local);
		if (engine == NULL) {
			g_debug("%s", error_local->message);
			continue;
		}
		if (fu_jcat_engine_get_method(engine) != FWUPD_JCAT_BLOB_METHOD_SIGNATURE)
			continue;
		result = fu_jcat_engine_pubkey_verify(engine,
						      data,
						      fwupd_jcat_blob_get_data(blob),
						      flags,
						      &error_local);
		if (result == NULL) {
			g_debug("signature failure: %s", error_local->message);
			continue;
		}
		result_str = fwupd_codec_to_string(FWUPD_CODEC(result));
		g_debug("verified: %s", result_str);
		g_ptr_array_add(results, g_steal_pointer(&result));
		nr_signature++;
	}
	if (flags & FU_JCAT_VERIFY_FLAG_REQUIRE_SIGNATURE && nr_signature == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "signatures were required, but none verified");
		return NULL;
	}

	/* at least one blob must have verified */
	if (results->len == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "no valid checksums or signatures found");
		return NULL;
	}

	/* success */
	return g_steal_pointer(&results);
}

/**
 * fu_jcat_context_verify_target:
 * @self: #FuJcatContext
 * @item_target: #FwupdJcatItem containing checksums of the data
 * @item: #FwupdJcatItem
 * @flags: #FuJcatVerifyFlags, e.g. %FU_JCAT_VERIFY_FLAG_REQUIRE_SIGNATURE
 * @error: #GError, or %NULL
 *
 * Verifies a #FwupdJcatItem using the target to an item. At least one `verify=CHECKSUM` (e.g.
 * SHA256) must exist and all checksum types that do exist must verify correctly.
 *
 * Returns: (transfer container) (element-type FuJcatResult): results, or %NULL for failed
 **/
GPtrArray *
fu_jcat_context_verify_target(FuJcatContext *self,
			      FwupdJcatItem *item_target,
			      FwupdJcatItem *item,
			      FuJcatVerifyFlags flags,
			      GError **error)
{
	guint nr_signature = 0;
	g_autoptr(GPtrArray) blobs = NULL;
	g_autoptr(GPtrArray) results =
	    g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);

	g_return_val_if_fail(FU_IS_JCAT_CONTEXT(self), NULL);
	g_return_val_if_fail(FWUPD_IS_JCAT_ITEM(item_target), NULL);
	g_return_val_if_fail(FWUPD_IS_JCAT_ITEM(item), NULL);

	/* no blobs */
	blobs = fwupd_jcat_item_get_blobs(item);
	if (blobs->len == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no blobs in item");
		return NULL;
	}

	/* all checksum engines must verify */
	for (guint i = 0; i < blobs->len; i++) {
		FwupdJcatBlob *blob = g_ptr_array_index(blobs, i);
		g_autoptr(GError) error_local = NULL;
		g_autoptr(FuJcatEngine) engine = NULL;
		g_autoptr(FwupdJcatBlob) blob_target = NULL;
		g_autofree gchar *checksum = NULL;
		g_autofree gchar *checksum_target = NULL;

		/* get engine */
		engine =
		    fu_jcat_context_get_engine(self, fwupd_jcat_blob_get_kind(blob), &error_local);
		if (engine == NULL) {
			g_debug("%s", error_local->message);
			continue;
		}
		if (fu_jcat_engine_get_method(engine) != FWUPD_JCAT_BLOB_METHOD_CHECKSUM)
			continue;
		blob_target = fwupd_jcat_item_get_blob_by_kind(item_target,
							       fwupd_jcat_blob_get_kind(blob),
							       &error_local);
		if (blob_target == NULL) {
			g_debug("no target value: %s", error_local->message);
			continue;
		}

		/* checksum is as expected */
		checksum = fwupd_jcat_blob_get_data_as_string(blob);
		checksum_target = fwupd_jcat_blob_get_data_as_string(blob_target);
		if (checksum == NULL || checksum_target == NULL) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "checksum data is missing");
			return NULL;
		}
		if (g_strcmp0(checksum, checksum_target) != 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "%s checksum was %s but target is %s",
				    fwupd_jcat_blob_kind_to_string(fwupd_jcat_blob_get_kind(blob)),
				    checksum,
				    checksum_target);
			return NULL;
		}
		g_ptr_array_add(results, g_object_new(FU_TYPE_JCAT_RESULT, "engine", engine, NULL));
	}
	if (flags & FU_JCAT_VERIFY_FLAG_REQUIRE_CHECKSUM && results->len == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "checksums were required, but none supplied");
		return NULL;
	}

	/* we only have to have one non-checksum method to verify */
	for (guint i = 0; i < blobs->len; i++) {
		FwupdJcatBlob *blob = g_ptr_array_index(blobs, i);
		g_autofree gchar *result_str = NULL;
		g_autoptr(GError) error_local = NULL;
		g_autoptr(FwupdJcatBlob) blob_target = NULL;
		g_autoptr(FuJcatEngine) engine = NULL;
		g_autoptr(FuJcatResult) result = NULL;

		engine =
		    fu_jcat_context_get_engine(self, fwupd_jcat_blob_get_kind(blob), &error_local);
		if (engine == NULL) {
			g_debug("%s", error_local->message);
			continue;
		}
		if (fu_jcat_engine_get_method(engine) != FWUPD_JCAT_BLOB_METHOD_SIGNATURE)
			continue;
		if (fwupd_jcat_blob_get_target(blob) == FWUPD_JCAT_BLOB_KIND_UNKNOWN) {
			g_debug("blob has no target");
			continue;
		}
		blob_target = fwupd_jcat_item_get_blob_by_kind(item_target,
							       fwupd_jcat_blob_get_target(blob),
							       &error_local);
		if (blob_target == NULL) {
			g_debug("no target for %s: %s",
				fwupd_jcat_blob_kind_to_string(fwupd_jcat_blob_get_target(blob)),
				error_local->message);
			continue;
		}
		result = fu_jcat_engine_pubkey_verify(engine,
						      fwupd_jcat_blob_get_data(blob_target),
						      fwupd_jcat_blob_get_data(blob),
						      flags,
						      &error_local);
		if (result == NULL) {
			g_debug("signature failure: %s", error_local->message);
			continue;
		}
		result_str = fwupd_codec_to_string(FWUPD_CODEC(result));
		g_debug("verified: %s", result_str);
		g_ptr_array_add(results, g_steal_pointer(&result));
		nr_signature++;
	}
	if (flags & FU_JCAT_VERIFY_FLAG_REQUIRE_SIGNATURE && nr_signature == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "signatures were required, but none verified");
		return NULL;
	}

	/* at least one blob must have verified */
	if (results->len == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "no valid checksums or signatures found");
		return NULL;
	}

	/* success */
	return g_steal_pointer(&results);
}

/**
 * fu_jcat_context_new:
 *
 * Creates a new context.
 *
 * Returns: a #FuJcatContext
 **/
FuJcatContext *
fu_jcat_context_new(void)
{
	return g_object_new(FU_TYPE_JCAT_CONTEXT, NULL);
}
