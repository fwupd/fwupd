/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-jcat-context.h"
#include "fu-jcat-engine.h"

typedef struct {
	FuJcatContext *context; /* weak */
	FwupdJcatBlobKind kind;
	FwupdJcatBlobMethod method;
	gboolean done_setup;
} FuJcatEnginePrivate;

static void
fu_jcat_engine_codec_iface_init(FwupdCodecInterface *iface);

G_DEFINE_TYPE_EXTENDED(FuJcatEngine,
		       fu_jcat_engine,
		       G_TYPE_OBJECT,
		       0,
		       G_ADD_PRIVATE(FuJcatEngine)
			   G_IMPLEMENT_INTERFACE(FWUPD_TYPE_CODEC,
						 fu_jcat_engine_codec_iface_init));
#define GET_PRIVATE(o) (fu_jcat_engine_get_instance_private(o))

enum { PROP_0, PROP_CONTEXT, PROP_KIND, PROP_METHOD, PROP_LAST };

static void
fu_jcat_engine_add_string(FwupdCodec *codec, guint idt, GString *str)
{
	FuJcatEngine *self = FU_JCAT_ENGINE(codec);
	FuJcatEnginePrivate *priv = GET_PRIVATE(self);
	fwupd_codec_string_append(str, idt, G_OBJECT_TYPE_NAME(self), NULL);
	fwupd_codec_string_append(str, idt + 1, "Kind", fwupd_jcat_blob_kind_to_string(priv->kind));
	fwupd_codec_string_append(str,
				  idt + 1,
				  "VerifyKind",
				  fwupd_jcat_blob_method_to_string(priv->method));
}

static gboolean
fu_jcat_engine_setup(FuJcatEngine *self, GError **error)
{
	FuJcatEngineClass *klass = FU_JCAT_ENGINE_GET_CLASS(self);
	FuJcatEnginePrivate *priv = GET_PRIVATE(self);

	g_return_val_if_fail(FU_IS_JCAT_ENGINE(self), FALSE);

	/* already done */
	if (priv->done_setup)
		return TRUE;

	/* sanity check */
	if (priv->context == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "no context");
		return FALSE;
	}

	/* optional */
	if (klass->setup != NULL) {
		if (!klass->setup(self, error))
			return FALSE;
	}
	if (klass->add_public_key != NULL) {
		GPtrArray *fns = fu_jcat_context_get_public_keys(priv->context);
		for (guint i = 0; i < fns->len; i++) {
			const gchar *fn = g_ptr_array_index(fns, i);
			if (!klass->add_public_key(self, fn, error))
				return FALSE;
		}
	}

	/* success */
	priv->done_setup = TRUE;
	return TRUE;
}

/**
 * fu_jcat_engine_pubkey_verify:
 * @self: #FuJcatEngine
 * @blob: #GBytes
 * @blob_signature: #GBytes
 * @flags: #FuJcatVerifyFlags, e.g. %FU_JCAT_VERIFY_FLAG_DISABLE_TIME_CHECKS
 * @error: #GError, or %NULL
 *
 * Verifies a chunk of data.
 *
 * Returns: (transfer full): #FuJcatResult, or %NULL for failed
 **/
FuJcatResult *
fu_jcat_engine_pubkey_verify(FuJcatEngine *self,
			     GBytes *blob,
			     GBytes *blob_signature,
			     FuJcatVerifyFlags flags,
			     GError **error)
{
	FuJcatEngineClass *klass = FU_JCAT_ENGINE_GET_CLASS(self);
	g_return_val_if_fail(FU_IS_JCAT_ENGINE(self), NULL);
	g_return_val_if_fail(blob != NULL, NULL);
	g_return_val_if_fail(blob_signature != NULL, NULL);
	if (klass->pubkey_verify == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "verifying data is not supported");
		return NULL;
	}
	if (!fu_jcat_engine_setup(self, error))
		return NULL;
	return klass->pubkey_verify(self, blob, blob_signature, flags, error);
}

/**
 * fu_jcat_engine_pubkey_sign:
 * @self: #FuJcatEngine
 * @blob: #GBytes
 * @cert: #GBytes
 * @privkey: #GBytes
 * @flags: #FuJcatSignFlags, e.g. %FU_JCAT_SIGN_FLAG_ADD_TIMESTAMP
 * @error: #GError, or %NULL
 *
 * Signs a chunk of data.
 *
 * Returns: (transfer full): #FwupdJcatBlob, or %NULL for failed
 **/
FwupdJcatBlob *
fu_jcat_engine_pubkey_sign(FuJcatEngine *self,
			   GBytes *blob,
			   GBytes *cert,
			   GBytes *privkey,
			   FuJcatSignFlags flags,
			   GError **error)
{
	FuJcatEngineClass *klass = FU_JCAT_ENGINE_GET_CLASS(self);
	g_return_val_if_fail(FU_IS_JCAT_ENGINE(self), NULL);
	g_return_val_if_fail(blob != NULL, NULL);
	g_return_val_if_fail(cert != NULL, NULL);
	g_return_val_if_fail(privkey != NULL, NULL);
	if (klass->pubkey_sign == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "signing data is not supported");
		return NULL;
	}
	if (!fu_jcat_engine_setup(self, error))
		return NULL;
	return klass->pubkey_sign(self, blob, cert, privkey, flags, error);
}

/**
 * fu_jcat_engine_self_verify:
 * @self: #FuJcatEngine
 * @blob: #GBytes
 * @blob_signature: #GBytes
 * @flags: #FuJcatVerifyFlags, e.g. %FU_JCAT_VERIFY_FLAG_DISABLE_TIME_CHECKS
 * @error: #GError, or %NULL
 *
 * Verifies a chunk of data.
 *
 * Returns: (transfer full): #FuJcatResult, or %NULL for failed
 **/
FuJcatResult *
fu_jcat_engine_self_verify(FuJcatEngine *self,
			   GBytes *blob,
			   GBytes *blob_signature,
			   FuJcatVerifyFlags flags,
			   GError **error)
{
	FuJcatEngineClass *klass = FU_JCAT_ENGINE_GET_CLASS(self);
	g_return_val_if_fail(FU_IS_JCAT_ENGINE(self), NULL);
	g_return_val_if_fail(blob != NULL, NULL);
	g_return_val_if_fail(blob_signature != NULL, NULL);
	if (klass->self_verify == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "verifying data is not supported");
		return NULL;
	}
	if (!fu_jcat_engine_setup(self, error))
		return NULL;
	return klass->self_verify(self, blob, blob_signature, flags, error);
}

/**
 * fu_jcat_engine_self_sign:
 * @self: #FuJcatEngine
 * @blob: #GBytes
 * @flags: #FuJcatSignFlags, e.g. %FU_JCAT_SIGN_FLAG_ADD_TIMESTAMP
 * @error: #GError, or %NULL
 *
 * Signs a chunk of data.
 *
 * Returns: (transfer full): #FwupdJcatBlob, or %NULL for failed
 **/
FwupdJcatBlob *
fu_jcat_engine_self_sign(FuJcatEngine *self, GBytes *blob, FuJcatSignFlags flags, GError **error)
{
	FuJcatEngineClass *klass = FU_JCAT_ENGINE_GET_CLASS(self);
	g_return_val_if_fail(FU_IS_JCAT_ENGINE(self), NULL);
	g_return_val_if_fail(blob != NULL, NULL);
	if (klass->self_sign == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "signing data is not supported");
		return NULL;
	}
	if (!fu_jcat_engine_setup(self, error))
		return NULL;
	return klass->self_sign(self, blob, flags, error);
}

/**
 * fu_jcat_engine_add_public_key_raw:
 * @self: #FuJcatEngine
 * @blob: #GBytes
 * @error: #GError, or %NULL
 *
 * Adds a public key manually.
 *
 * Returns: %TRUE for success
 **/
gboolean
fu_jcat_engine_add_public_key_raw(FuJcatEngine *self, GBytes *blob, GError **error)
{
	FuJcatEngineClass *klass = FU_JCAT_ENGINE_GET_CLASS(self);
	g_return_val_if_fail(FU_IS_JCAT_ENGINE(self), FALSE);
	g_return_val_if_fail(blob != NULL, FALSE);
	if (klass->add_public_key_raw == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "adding public keys manually is not supported");
		return FALSE;
	}
	if (!fu_jcat_engine_setup(self, error))
		return FALSE;
	return klass->add_public_key_raw(self, blob, error);
}

/**
 * fu_jcat_engine_get_kind:
 * @self: #FuJcatEngine
 *
 * Gets the blob kind.
 *
 * Returns: #FwupdJcatBlobKind, e.g. %FWUPD_JCAT_BLOB_KIND_SHA256
 **/
FwupdJcatBlobKind
fu_jcat_engine_get_kind(FuJcatEngine *self)
{
	FuJcatEnginePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_JCAT_ENGINE(self), FWUPD_JCAT_BLOB_KIND_UNKNOWN);
	return priv->kind;
}

/**
 * fu_jcat_engine_get_method:
 * @self: #FuJcatEngine
 *
 * Gets the verification method.
 *
 * Returns: #FwupdJcatBlobMethod, e.g. %FWUPD_JCAT_BLOB_METHOD_SIGNATURE
 **/
FwupdJcatBlobMethod
fu_jcat_engine_get_method(FuJcatEngine *self)
{
	FuJcatEnginePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_JCAT_ENGINE(self), FWUPD_JCAT_BLOB_METHOD_UNKNOWN);
	return priv->method;
}

/**
 * fu_jcat_engine_get_keyring_path:
 * @self: #FuJcatEngine
 *
 * Gets the keyring path.
 *
 * Returns: keyring path, or %NULL if not set
 **/
const gchar *
fu_jcat_engine_get_keyring_path(FuJcatEngine *self)
{
	FuJcatEnginePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_JCAT_ENGINE(self), NULL);
	if (priv->context == NULL)
		return NULL;
	return fu_jcat_context_get_keyring_path(priv->context);
}

static void
fu_jcat_engine_finalize(GObject *object)
{
	G_OBJECT_CLASS(fu_jcat_engine_parent_class)->finalize(object);
}

static void
fu_jcat_engine_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	FuJcatEngine *self = FU_JCAT_ENGINE(object);
	FuJcatEnginePrivate *priv = GET_PRIVATE(self);
	switch (prop_id) {
	case PROP_CONTEXT:
		g_value_set_object(value, priv->context);
		break;
	case PROP_KIND:
		g_value_set_uint(value, priv->kind);
		break;
	case PROP_METHOD:
		g_value_set_uint(value, priv->method);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fu_jcat_engine_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	FuJcatEngine *self = FU_JCAT_ENGINE(object);
	FuJcatEnginePrivate *priv = GET_PRIVATE(self);
	switch (prop_id) {
	case PROP_CONTEXT:
		/* weak */
		priv->context = g_value_get_object(value);
		break;
	case PROP_KIND:
		priv->kind = g_value_get_uint(value);
		break;
	case PROP_METHOD:
		priv->method = g_value_get_uint(value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fu_jcat_engine_codec_iface_init(FwupdCodecInterface *iface)
{
	iface->add_string = fu_jcat_engine_add_string;
}

static void
fu_jcat_engine_class_init(FuJcatEngineClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	GParamSpec *pspec;

	object_class->get_property = fu_jcat_engine_get_property;
	object_class->set_property = fu_jcat_engine_set_property;

	pspec =
	    g_param_spec_object("context",
				NULL,
				NULL,
				FU_TYPE_JCAT_CONTEXT,
				G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_CONTEXT, pspec);

	pspec = g_param_spec_uint("kind",
				  NULL,
				  NULL,
				  0,
				  G_MAXUINT,
				  0,
				  G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_KIND, pspec);

	pspec = g_param_spec_uint("method",
				  NULL,
				  NULL,
				  FWUPD_JCAT_BLOB_METHOD_UNKNOWN,
				  FWUPD_JCAT_BLOB_METHOD_LAST,
				  FWUPD_JCAT_BLOB_METHOD_UNKNOWN,
				  G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_METHOD, pspec);

	object_class->finalize = fu_jcat_engine_finalize;
}

static void
fu_jcat_engine_init(FuJcatEngine *self)
{
}
