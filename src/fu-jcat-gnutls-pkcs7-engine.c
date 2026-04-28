/*
 * Copyright (C) 2017-2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-jcat-engine.h"
#include "fu-jcat-gnutls-common.h"
#include "fu-jcat-gnutls-pkcs7-engine.h"

#ifdef HAVE_GNUTLS
#include <gnutls/abstract.h>
#include <gnutls/crypto.h>
#endif

struct _FuJcatGnutlsPkcs7Engine {
	FuJcatEngine parent_instance;
	GPtrArray *pubkeys_crts; /* element-type gnutls_x509_crt_t */
};

G_DEFINE_TYPE(FuJcatGnutlsPkcs7Engine, fu_jcat_gnutls_pkcs7_engine, FU_TYPE_JCAT_ENGINE)

static gboolean
fu_jcat_gnutls_pkcs7_engine_add_pubkey_blob_fmt(FuJcatGnutlsPkcs7Engine *self,
						GBytes *blob,
						gnutls_x509_crt_fmt_t format,
						GError **error)
{
	guint key_usage = 0;
	int rc;
	g_auto(gnutls_x509_crt_t) crt = NULL;

	/* load file and add to the trust list */
	crt = fu_jcat_gnutls_pkcs7_load_crt_from_blob(blob, format, error);
	if (crt == NULL)
		return FALSE;
	rc = gnutls_x509_crt_get_key_usage(crt, &key_usage, NULL);
	if (!fu_jcat_gnutls_rc_to_error(rc, error)) {
		g_prefix_error_literal(error, "failed to get key usage: ");
		return FALSE;
	}
	if ((key_usage & GNUTLS_KEY_DIGITAL_SIGNATURE) == 0 &&
	    (key_usage & GNUTLS_KEY_KEY_CERT_SIGN) == 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "certificate not suitable for use [0x%x]",
			    key_usage);
		return FALSE;
	}
	g_ptr_array_add(self->pubkeys_crts, g_steal_pointer(&crt));
	return TRUE;
}

static gboolean
fu_jcat_gnutls_pkcs7_engine_add_public_key_raw(FuJcatEngine *engine, GBytes *blob, GError **error)
{
	FuJcatGnutlsPkcs7Engine *self = FU_JCAT_GNUTLS_PKCS7_ENGINE(engine);
	return fu_jcat_gnutls_pkcs7_engine_add_pubkey_blob_fmt(self,
							       blob,
							       GNUTLS_X509_FMT_PEM,
							       error);
}

static gboolean
fu_jcat_gnutls_pkcs7_engine_add_public_key(FuJcatEngine *engine,
					   const gchar *filename,
					   GError **error)
{
	FuJcatGnutlsPkcs7Engine *self = FU_JCAT_GNUTLS_PKCS7_ENGINE(engine);

	/* search all the public key files */
	if (g_str_has_suffix(filename, ".pem") || g_str_has_suffix(filename, ".crt")) {
		g_autoptr(GBytes) blob = fu_bytes_get_contents(filename, error);
		if (blob == NULL)
			return FALSE;
		if (!fu_jcat_gnutls_pkcs7_engine_add_pubkey_blob_fmt(self,
								     blob,
								     GNUTLS_X509_FMT_PEM,
								     error))
			return FALSE;
	} else if (g_str_has_suffix(filename, ".cer") || g_str_has_suffix(filename, ".der")) {
		g_autoptr(GBytes) blob = fu_bytes_get_contents(filename, error);
		if (blob == NULL)
			return FALSE;
		if (!fu_jcat_gnutls_pkcs7_engine_add_pubkey_blob_fmt(self,
								     blob,
								     GNUTLS_X509_FMT_DER,
								     error))
			return FALSE;
	} else {
		g_autofree gchar *basename = g_path_get_basename(filename);
		g_debug("ignoring %s as not PKCS-7 certificate", basename);
	}
	return TRUE;
}

static gnutls_x509_trust_list_t
fu_jcat_gnutls_pkcs7_engine_build_trust_list(FuJcatGnutlsPkcs7Engine *self, GError **error)
{
	int rc;
	g_auto(gnutls_x509_trust_list_t) tl = NULL;

	rc = gnutls_x509_trust_list_init(&tl, 0);
	if (!fu_jcat_gnutls_rc_to_error(rc, error)) {
		g_prefix_error_literal(error, "failed to create trust list: ");
		return NULL;
	}
	rc = gnutls_x509_trust_list_add_cas(tl,
					    (const gnutls_x509_crt_t *)self->pubkeys_crts->pdata,
					    self->pubkeys_crts->len,
					    0);
	if (!fu_jcat_gnutls_rc_to_error(rc, error)) {
		g_prefix_error_literal(error, "failed to add to trust list: ");
		return NULL;
	}
	g_debug("loaded %i certificates", rc);

	/* success */
	return g_steal_pointer(&tl);
}

#ifdef HAVE_GNUTLS_PQC
static gnutls_x509_trust_list_t
fu_jcat_gnutls_pkcs7_engine_build_trust_list_only_pq(FuJcatGnutlsPkcs7Engine *self, GError **error)
{
	int rc;
	g_auto(gnutls_x509_trust_list_t) tl = NULL;

	rc = gnutls_x509_trust_list_init(&tl, 0);
	if (!fu_jcat_gnutls_rc_to_error(rc, error)) {
		g_prefix_error_literal(error, "failed to create trust list: ");
		return NULL;
	}
	for (guint i = 0; i < self->pubkeys_crts->len; i++) {
		gnutls_x509_crt_t crt = g_ptr_array_index(self->pubkeys_crts, i);
		gnutls_sign_algorithm_t algo = gnutls_x509_crt_get_signature_algorithm(crt);

		if (algo != GNUTLS_SIGN_MLDSA44 && algo != GNUTLS_SIGN_MLDSA65 &&
		    algo != GNUTLS_SIGN_MLDSA87)
			continue;
		rc = gnutls_x509_trust_list_add_cas(tl, &crt, 1, 0);
		if (!fu_jcat_gnutls_rc_to_error(rc, error)) {
			g_prefix_error_literal(error, "failed to add to trust list: ");
			return NULL;
		}
		g_debug("loaded %i certificates", rc);
	}

	/* success */
	return g_steal_pointer(&tl);
}
#endif

/* verifies a detached signature just like:
 *  `certtool --p7-verify --load-certificate client.pem --infile=test.p7b` */
static FuJcatResult *
fu_jcat_gnutls_pkcs7_engine_verify(FuJcatEngine *engine,
				   GBytes *blob,
				   GBytes *blob_signature,
				   gnutls_x509_crt_t crt,
				   FuJcatVerifyFlags flags,
				   GError **error)
{
	FuJcatGnutlsPkcs7Engine *self = FU_JCAT_GNUTLS_PKCS7_ENGINE(engine);
	gnutls_datum_t datum = {0};
	gint64 timestamp_newest = 0;
	int count;
	int rc;
	g_auto(gnutls_pkcs7_t) pkcs7 = NULL;
	g_autoptr(GString) authority_newest = g_string_new(NULL);

	/* startup */
	rc = gnutls_pkcs7_init(&pkcs7);
	if (!fu_jcat_gnutls_rc_to_error(rc, error)) {
		g_prefix_error_literal(error, "failed to init pkcs7: ");
		return NULL;
	}

	/* import the signature */
	if (!fu_jcat_gnutls_datum_from_bytes(&datum, blob_signature, error))
		return NULL;
	rc = gnutls_pkcs7_import(pkcs7, &datum, GNUTLS_X509_FMT_PEM);
	if (!fu_jcat_gnutls_rc_to_error(rc, error)) {
		g_prefix_error_literal(error, "failed to import the PKCS7 signature: ");
		return NULL;
	}

	/* verify the blob */
	if (!fu_jcat_gnutls_datum_from_bytes(&datum, blob, error))
		return NULL;
	count = gnutls_pkcs7_get_signature_count(pkcs7);
	g_debug("got %i PKCS7 signatures", count);
	if (count <= 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "no PKCS7 signatures found");
		return NULL;
	}
	for (gint i = 0; i < count; i++) {
		gnutls_pkcs7_signature_info_st info_tmp = {0x0};
		g_autoptr(gnutls_pkcs7_signature_info_st) info = &info_tmp;
		gnutls_certificate_verify_flags verify_flags = 0;
		g_autofree gchar *dn = NULL;

		/* use with care */
		if (flags & FU_JCAT_VERIFY_FLAG_DISABLE_TIME_CHECKS) {
			g_debug("WARNING: disabling time checks");
			verify_flags |= GNUTLS_VERIFY_DISABLE_TIME_CHECKS;
			verify_flags |= GNUTLS_VERIFY_DISABLE_TRUSTED_TIME_CHECKS;
		}

		/* always get issuer */
		rc = gnutls_pkcs7_get_signature_info(pkcs7, i, &info_tmp);
		if (!fu_jcat_gnutls_rc_to_error(rc, error)) {
			g_prefix_error_literal(error, "failed to get signature info: ");
			return NULL;
		}

		/* verify the data against the detached signature */
		if (crt != NULL) {
			rc = gnutls_pkcs7_verify_direct(pkcs7, crt, i, &datum, verify_flags);
		} else {
			g_auto(gnutls_x509_trust_list_t) tl = NULL;
			if (flags & FU_JCAT_VERIFY_FLAG_ONLY_PQ) {
#ifdef HAVE_GNUTLS_PQC
				tl = fu_jcat_gnutls_pkcs7_engine_build_trust_list_only_pq(self,
											  error);
				if (tl == NULL)
					return NULL;
#else
				g_set_error_literal(error,
						    FWUPD_ERROR,
						    FWUPD_ERROR_NOT_SUPPORTED,
						    "GnuTLS too old for PQC support");
				return NULL;
#endif
			} else {
				tl = fu_jcat_gnutls_pkcs7_engine_build_trust_list(self, error);
				if (tl == NULL)
					return NULL;
			}
			if (!fu_jcat_gnutls_ensure_trust_list_valid(tl, error))
				return NULL;
			rc = gnutls_pkcs7_verify(pkcs7,
						 tl,
						 NULL,	 /* vdata */
						 0,	 /* vdata_size */
						 i,	 /* index */
						 &datum, /* data */
						 verify_flags);
		}
		if (!fu_jcat_gnutls_rc_to_error(rc, error)) {
			dn = fu_jcat_gnutls_pkcs7_datum_to_dn_str(&info->issuer_dn);
			if (dn != NULL) {
				/* nocheck:error */
				g_prefix_error(error, "failed to verify data for %s: ", dn);
			} else {
				/* nocheck:error */
				g_prefix_error_literal(error, "failed to verify data: ");
			}
			return NULL;
		}

		/* save details about the key for the result */
		if (info->signing_time > timestamp_newest || timestamp_newest == 0) {
			timestamp_newest = info->signing_time;
			dn = fu_jcat_gnutls_pkcs7_datum_to_dn_str(&info->issuer_dn);
			if (dn != NULL)
				g_string_assign(authority_newest, dn);
		}
	}

	/* success */
	return FU_JCAT_RESULT(g_object_new(FU_TYPE_JCAT_RESULT,
					   "engine",
					   engine,
					   "timestamp",
					   timestamp_newest,
					   "authority",
					   authority_newest->str,
					   NULL));
}

/* verifies a detached signature just like:
 *  `certtool --p7-verify --load-certificate client.pem --infile=test.p7b` */
static FuJcatResult *
fu_jcat_gnutls_pkcs7_engine_self_verify(FuJcatEngine *engine,
					GBytes *blob,
					GBytes *blob_signature,
					FuJcatVerifyFlags flags,
					GError **error)
{
	g_autofree gchar *filename = NULL;
	g_auto(gnutls_x509_crt_t) crt = NULL;
	g_autoptr(GBytes) cert_blob = NULL;

	if (flags & FU_JCAT_VERIFY_FLAG_ONLY_PQ) {
		filename = g_build_filename(fu_jcat_engine_get_keyring_path(engine),
					    "pki",
					    "client-mldsa87.pem",
					    NULL);
	} else {
		filename = g_build_filename(fu_jcat_engine_get_keyring_path(engine),
					    "pki",
					    "client.pem",
					    NULL);
	}
	cert_blob = fu_bytes_get_contents(filename, error);
	if (cert_blob == NULL)
		return NULL;
	crt = fu_jcat_gnutls_pkcs7_load_crt_from_blob(cert_blob, GNUTLS_X509_FMT_PEM, error);
	if (crt == NULL)
		return NULL;
	if (flags & FU_JCAT_VERIFY_FLAG_ONLY_PQ) {
		if (!fu_jcat_gnutls_pkcs7_ensure_sign_algo_pq_safe(
			gnutls_x509_crt_get_signature_algorithm(crt),
			error))
			return NULL;
	}
	return fu_jcat_gnutls_pkcs7_engine_verify(engine, blob, blob_signature, crt, flags, error);
}

/* verifies a detached signature just like:
 *  `certtool --p7-verify --load-certificate client.pem --infile=test.p7b` */
static FuJcatResult *
fu_jcat_gnutls_pkcs7_engine_pubkey_verify(FuJcatEngine *engine,
					  GBytes *blob,
					  GBytes *blob_signature,
					  FuJcatVerifyFlags flags,
					  GError **error)
{
	return fu_jcat_gnutls_pkcs7_engine_verify(engine, blob, blob_signature, NULL, flags, error);
}

static FwupdJcatBlob *
fu_jcat_gnutls_pkcs7_engine_pubkey_sign(FuJcatEngine *engine,
					GBytes *blob,
					GBytes *cert,
					GBytes *privkey,
					FuJcatSignFlags flags,
					GError **error)
{
	gnutls_datum_t d = {0};
	gnutls_digest_algorithm_t dig = GNUTLS_DIG_NULL;
	guint gnutls_flags = 0;
	int rc;
	g_autofree gchar *str = NULL;
	g_auto(gnutls_pkcs7_t) pkcs7 = NULL;
	g_auto(gnutls_privkey_t) key = NULL;
	g_auto(gnutls_pubkey_t) pubkey = NULL;
	g_auto(gnutls_x509_crt_t) crt = NULL;
	g_autoptr(gnutls_data_t) d_payload = NULL;

	/* nothing to do */
	if (g_bytes_get_size(blob) == 0) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOTHING_TO_DO, "nothing to do");
		return NULL;
	}

	/* load keys */
	key = fu_jcat_gnutls_pkcs7_load_privkey_from_blob(privkey, error);
	if (key == NULL)
		return NULL;
	crt = fu_jcat_gnutls_pkcs7_load_crt_from_blob(cert, GNUTLS_X509_FMT_PEM, error);
	if (crt == NULL)
		return NULL;

	/* get the digest algorithm from the public key */
	pubkey = fu_jcat_gnutls_pkcs7_load_pubkey_from_privkey(key, error);
	if (pubkey == NULL)
		return NULL;
	rc = gnutls_pubkey_get_preferred_hash_algorithm(pubkey, &dig, NULL);
	if (!fu_jcat_gnutls_rc_to_error(rc, error)) {
		g_prefix_error_literal(error, "failed to get preferred hash algorithm: ");
		return NULL;
	}
	g_debug("preferred_hash_algorithm=%s", gnutls_digest_get_name(dig));

	/* create container */
	rc = gnutls_pkcs7_init(&pkcs7);
	if (!fu_jcat_gnutls_rc_to_error(rc, error)) {
		g_prefix_error_literal(error, "failed to pkcs7_init: ");
		return NULL;
	}

	/* sign data */
	if (!fu_jcat_gnutls_datum_from_bytes(&d, blob, error))
		return NULL;
	if (flags & FU_JCAT_SIGN_FLAG_ADD_TIMESTAMP)
		gnutls_flags |= GNUTLS_PKCS7_INCLUDE_TIME;
	if (flags & FU_JCAT_SIGN_FLAG_ADD_CERT)
		gnutls_flags |= GNUTLS_PKCS7_INCLUDE_CERT;
	rc = gnutls_pkcs7_sign(pkcs7, crt, key, &d, NULL, NULL, dig, gnutls_flags);
	if (!fu_jcat_gnutls_rc_to_error(rc, error)) {
		g_prefix_error_literal(error, "failed to pkcs7_sign: ");
		return NULL;
	}

	/* export */
	rc = gnutls_pkcs7_export2(pkcs7, GNUTLS_X509_FMT_PEM, &d);
	if (!fu_jcat_gnutls_rc_to_error(rc, error)) {
		g_prefix_error_literal(error, "failed to pkcs7_export: ");
		return NULL;
	}
	if (d.size == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "pkcs7 export produced empty output");
		return NULL;
	}
	d_payload = d.data;
	str = g_strndup((const gchar *)d_payload, d.size);
	return fwupd_jcat_blob_new_utf8(FWUPD_JCAT_BLOB_KIND_PKCS7, str);
}

/* creates a detached signature just like:
 *  `certtool --p7-detached-sign --load-certificate client.pem \
 *    --load-privkey secret.pem --outfile=test.p7b` */
static FwupdJcatBlob *
fu_jcat_gnutls_pkcs7_engine_self_sign(FuJcatEngine *engine,
				      GBytes *blob,
				      FuJcatSignFlags flags,
				      GError **error)
{
	g_autofree gchar *fn_cert = NULL;
	g_autofree gchar *fn_privkey = NULL;
	g_autoptr(GBytes) cert = NULL;
	g_autoptr(GBytes) privkey = NULL;

	/* check private key exists, otherwise generate and save */
	if (flags & FU_JCAT_SIGN_FLAG_USE_PQ) {
		fn_privkey = g_build_filename(fu_jcat_engine_get_keyring_path(engine),
					      "pki",
					      "secret-mldsa87.key",
					      NULL);
	} else {
		fn_privkey = g_build_filename(fu_jcat_engine_get_keyring_path(engine),
					      "pki",
					      "secret.key",
					      NULL);
	}
	if (g_file_test(fn_privkey, G_FILE_TEST_EXISTS)) {
		privkey = fu_bytes_get_contents(fn_privkey, error);
		if (privkey == NULL)
			return NULL;
	} else {
		if (flags & FU_JCAT_SIGN_FLAG_USE_PQ) {
#ifdef HAVE_GNUTLS_PQC
			privkey = fu_jcat_gnutls_pkcs7_create_private_key(GNUTLS_PK_MLDSA87, error);
			if (privkey == NULL)
				return NULL;
#else
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "GnuTLS too old for PQC support");
			return NULL;
#endif
		} else {
			privkey = fu_jcat_gnutls_pkcs7_create_private_key(GNUTLS_PK_RSA, error);
			if (privkey == NULL)
				return NULL;
		}
		if (!fu_path_mkdir_parent(fn_privkey, error))
			return NULL;
		if (!fu_bytes_set_contents_full(fn_privkey, privkey, 0600, error))
			return NULL;
	}

	/* check client certificate exists, otherwise generate and save */
	if (flags & FU_JCAT_SIGN_FLAG_USE_PQ) {
		fn_cert = g_build_filename(fu_jcat_engine_get_keyring_path(engine),
					   "pki",
					   "client-mldsa87.pem",
					   NULL);
	} else {
		fn_cert = g_build_filename(fu_jcat_engine_get_keyring_path(engine),
					   "pki",
					   "client.pem",
					   NULL);
	}
	if (g_file_test(fn_cert, G_FILE_TEST_EXISTS)) {
		cert = fu_bytes_get_contents(fn_cert, error);
		if (cert == NULL)
			return NULL;
	} else {
		g_auto(gnutls_privkey_t) key = NULL;
		key = fu_jcat_gnutls_pkcs7_load_privkey_from_blob(privkey, error);
		if (key == NULL)
			return NULL;
		cert = fu_jcat_gnutls_pkcs7_create_client_certificate(key, error);
		if (cert == NULL)
			return NULL;
		if (!fu_path_mkdir_parent(fn_cert, error))
			return NULL;
		if (!fu_bytes_set_contents_full(fn_cert, cert, 0644, error))
			return NULL;
	}

	/* sign */
	return fu_jcat_gnutls_pkcs7_engine_pubkey_sign(engine, blob, cert, privkey, flags, error);
}

static void
fu_jcat_gnutls_pkcs7_engine_finalize(GObject *object)
{
	FuJcatGnutlsPkcs7Engine *self = FU_JCAT_GNUTLS_PKCS7_ENGINE(object);
	g_ptr_array_unref(self->pubkeys_crts);
	G_OBJECT_CLASS(fu_jcat_gnutls_pkcs7_engine_parent_class)->finalize(object);
}

static void
fu_jcat_gnutls_pkcs7_engine_class_init(FuJcatGnutlsPkcs7EngineClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuJcatEngineClass *engine_class = FU_JCAT_ENGINE_CLASS(klass);
	engine_class->add_public_key = fu_jcat_gnutls_pkcs7_engine_add_public_key;
	engine_class->add_public_key_raw = fu_jcat_gnutls_pkcs7_engine_add_public_key_raw;
	engine_class->pubkey_verify = fu_jcat_gnutls_pkcs7_engine_pubkey_verify;
	engine_class->pubkey_sign = fu_jcat_gnutls_pkcs7_engine_pubkey_sign;
	engine_class->self_verify = fu_jcat_gnutls_pkcs7_engine_self_verify;
	engine_class->self_sign = fu_jcat_gnutls_pkcs7_engine_self_sign;
	object_class->finalize = fu_jcat_gnutls_pkcs7_engine_finalize;
}

static void
fu_jcat_gnutls_pkcs7_engine_init(FuJcatGnutlsPkcs7Engine *self)
{
	fu_jcat_gnutls_global_init();
	self->pubkeys_crts = g_ptr_array_new_with_free_func((GDestroyNotify)gnutls_x509_crt_deinit);
}

FuJcatEngine *
fu_jcat_gnutls_pkcs7_engine_new(FuJcatContext *context)
{
	g_return_val_if_fail(FU_IS_JCAT_CONTEXT(context), NULL);
	return FU_JCAT_ENGINE(g_object_new(FU_TYPE_JCAT_GNUTLS_PKCS7_ENGINE,
					   "context",
					   context,
					   "kind",
					   FWUPD_JCAT_BLOB_KIND_PKCS7,
					   "method",
					   FWUPD_JCAT_BLOB_METHOD_SIGNATURE,
					   NULL));
}
