/*
 * Copyright (C) 2017-2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-jcat-engine.h"
#include "fu-jcat-mldsa87-engine.h"
#include "fu-jcat-pkcs7-common.h"

struct _FuJcatGnutlsMldsa87Engine {
	FuJcatEngine parent_instance;
	gnutls_x509_trust_list_t tl;
};

G_DEFINE_TYPE(FuJcatGnutlsMldsa87Engine, fu_jcat_gnutls_mldsa87_engine, FU_TYPE_JCAT_ENGINE)

static gboolean
fu_jcat_gnutls_mldsa87_engine_add_pubkey_blob_fmt(FuJcatGnutlsMldsa87Engine *self,
						  GBytes *blob,
						  gnutls_x509_crt_fmt_t format,
						  GError **error)
{
	guint key_usage = 0;
	int rc;
	g_auto(gnutls_x509_crt_t) crt = NULL;

	/* load file and add to the trust list */
	crt = fu_jcat_gnutls_load_crt_from_blob(blob, format, error);
	if (crt == NULL)
		return FALSE;
	rc = gnutls_x509_crt_get_key_usage(crt, &key_usage, NULL);
	if (rc < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "failed to get key usage: %s [%i]",
			    gnutls_strerror(rc),
			    rc);
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
	rc = gnutls_x509_trust_list_add_cas(self->tl, &crt, 1, 0);
	if (rc < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "failed to add to trust list: %s [%i]",
			    gnutls_strerror(rc),
			    rc);
		return FALSE;
	}
	g_debug("loaded %i certificates", rc);

	/* confusingly the trust list does not copy the certificate */
	crt = NULL;
	return TRUE;
}

static gboolean
fu_jcat_gnutls_mldsa87_engine_add_public_key_raw(FuJcatEngine *engine, GBytes *blob, GError **error)
{
	FuJcatGnutlsMldsa87Engine *self = FWUPD_JCAT_GNUTLS_MLDSA87_ENGINE(engine);
	return fu_jcat_gnutls_mldsa87_engine_add_pubkey_blob_fmt(self,
								 blob,
								 GNUTLS_X509_FMT_PEM,
								 error);
}

static gboolean
fu_jcat_gnutls_mldsa87_engine_add_public_key(FuJcatEngine *engine,
					     const gchar *filename,
					     GError **error)
{
	FuJcatGnutlsMldsa87Engine *self = FWUPD_JCAT_GNUTLS_MLDSA87_ENGINE(engine);

	/* search all the public key files */
	if (g_str_has_suffix(filename, ".pem")) {
		g_autoptr(GBytes) blob = fu_bytes_get_contents(filename, error);
		if (blob == NULL)
			return FALSE;
		if (!fu_jcat_gnutls_mldsa87_engine_add_pubkey_blob_fmt(self,
								       blob,
								       GNUTLS_X509_FMT_PEM,
								       error))
			return FALSE;
	} else if (g_str_has_suffix(filename, ".cer") || g_str_has_suffix(filename, ".crt") ||
		   g_str_has_suffix(filename, ".der")) {
		g_autoptr(GBytes) blob = fu_bytes_get_contents(filename, error);
		if (blob == NULL)
			return FALSE;
		if (!fu_jcat_gnutls_mldsa87_engine_add_pubkey_blob_fmt(self,
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

static gboolean
fu_jcat_gnutls_mldsa87_engine_setup(FuJcatEngine *engine, GError **error)
{
	FuJcatGnutlsMldsa87Engine *self = FWUPD_JCAT_GNUTLS_MLDSA87_ENGINE(engine);
	int rc;

	if (self->tl != NULL)
		return TRUE;

	/* create trust list, a bit like a engine */
	rc = gnutls_x509_trust_list_init(&self->tl, 0);
	if (rc != GNUTLS_E_SUCCESS) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "failed to create trust list: %s [%i]",
			    gnutls_strerror(rc),
			    rc);
		return FALSE;
	}
	return TRUE;
}

/* verifies a detached signature just like:
 *  `certtool --p7-verify --load-certificate client.pem --infile=test.p7b` */
static FuJcatResult *
fu_jcat_gnutls_mldsa87_engine_verify(FuJcatEngine *engine,
				     GBytes *blob,
				     GBytes *blob_signature,
				     gnutls_x509_crt_t crt,
				     FuJcatVerifyFlags flags,
				     GError **error)
{
	FuJcatGnutlsMldsa87Engine *self = FWUPD_JCAT_GNUTLS_MLDSA87_ENGINE(engine);
	gnutls_datum_t datum = {0};
	gint64 timestamp_newest = 0;
	gnutls_mldsa87_signature_info_st info_tmp = {0x0};
	int count;
	int rc;
	g_auto(gnutls_mldsa87_t) mldsa87 = NULL;
	g_autoptr(GString) authority_newest = g_string_new(NULL);

	/* startup */
	rc = gnutls_mldsa87_init(&mldsa87);
	if (rc != GNUTLS_E_SUCCESS) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "failed to init mldsa87: %s [%i]",
			    gnutls_strerror(rc),
			    rc);
		return NULL;
	}

	/* import the signature */
	datum.data = (guchar *)g_bytes_get_data(blob_signature, NULL);
	datum.size = g_bytes_get_size(blob_signature);
	rc = gnutls_mldsa87_import(mldsa87, &datum, GNUTLS_X509_FMT_PEM);
	if (rc != GNUTLS_E_SUCCESS) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "failed to import the MLDSA87 signature: %s [%i]",
			    gnutls_strerror(rc),
			    rc);
		return NULL;
	}

	/* verify the blob */
	datum.data = (guchar *)g_bytes_get_data(blob, NULL);
	datum.size = g_bytes_get_size(blob);
	count = gnutls_mldsa87_get_signature_count(mldsa87);
	g_debug("got %i MLDSA87 signatures", count);
	if (count == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "no MLDSA87 signatures found");
		return NULL;
	}
	for (gint i = 0; i < count; i++) {
		g_autoptr(gnutls_mldsa87_signature_info_st) info = &info_tmp;
		gint64 signing_time = 0;
		gnutls_certificate_verify_flags verify_flags = 0;
		g_autofree gchar *dn = NULL;

		/* use with care */
		if (flags & FU_JCAT_VERIFY_FLAG_DISABLE_TIME_CHECKS) {
			g_debug("WARNING: disabling time checks");
			verify_flags |= GNUTLS_VERIFY_DISABLE_TIME_CHECKS;
			verify_flags |= GNUTLS_VERIFY_DISABLE_TRUSTED_TIME_CHECKS;
		}

		/* always get issuer */
		rc = gnutls_mldsa87_get_signature_info(mldsa87, i, &info_tmp);
		if (rc < 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "failed to get signature info: %s [%i]",
				    gnutls_strerror(rc),
				    rc);
			return NULL;
		}

		/* verify the data against the detached signature */
		if (crt != NULL) {
			rc = gnutls_mldsa87_verify_direct(mldsa87, crt, i, &datum, 0);
		} else {
			rc = gnutls_mldsa87_verify(mldsa87,
						   self->tl,
						   NULL,   /* vdata */
						   0,	   /* vdata_size */
						   i,	   /* index */
						   &datum, /* data */
						   verify_flags);
		}
		if (rc < 0) {
			dn = fu_jcat_gnutls_datum_to_dn_str(&info->issuer_dn);
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "failed to verify data for %s: %s [%i]",
				    dn,
				    gnutls_strerror(rc),
				    rc);
			return NULL;
		}

		/* save details about the key for the result */
		signing_time = info->signing_time > 0 ? (gint64)info->signing_time : 1;
		if (signing_time > timestamp_newest) {
			timestamp_newest = signing_time;
			dn = fu_jcat_gnutls_datum_to_dn_str(&info->issuer_dn);
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
fu_jcat_gnutls_mldsa87_engine_self_verify(FuJcatEngine *engine,
					  GBytes *blob,
					  GBytes *blob_signature,
					  FuJcatVerifyFlags flags,
					  GError **error)
{
	g_autofree gchar *filename = NULL;
	g_auto(gnutls_x509_crt_t) crt = NULL;
	g_autoptr(GBytes) cert_blob = NULL;

	filename =
	    g_build_filename(fu_jcat_engine_get_keyring_path(engine), "pki", "client.pem", NULL);
	cert_blob = fu_bytes_get_contents(filename, error);
	if (cert_blob == NULL)
		return NULL;
	crt = fu_jcat_gnutls_load_crt_from_blob(cert_blob, GNUTLS_X509_FMT_PEM, error);
	if (crt == NULL)
		return NULL;

	return fu_jcat_gnutls_mldsa87_engine_verify(engine,
						    blob,
						    blob_signature,
						    crt,
						    flags,
						    error);
}

/* verifies a detached signature just like:
 *  `certtool --p7-verify --load-certificate client.pem --infile=test.p7b` */
static FuJcatResult *
fu_jcat_gnutls_mldsa87_engine_pubkey_verify(FuJcatEngine *engine,
					    GBytes *blob,
					    GBytes *blob_signature,
					    FuJcatVerifyFlags flags,
					    GError **error)
{
	return fu_jcat_gnutls_mldsa87_engine_verify(engine,
						    blob,
						    blob_signature,
						    NULL,
						    flags,
						    error);
}

static FwupdJcatBlob *
fu_jcat_gnutls_mldsa87_engine_pubkey_sign(FuJcatEngine *engine,
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
	g_auto(gnutls_mldsa87_t) mldsa87 = NULL;
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
	key = fu_jcat_gnutls_load_privkey_from_blob(privkey, error);
	if (key == NULL)
		return NULL;
	crt = fu_jcat_gnutls_load_crt_from_blob(cert, GNUTLS_X509_FMT_PEM, error);
	if (crt == NULL)
		return NULL;

	/* get the digest algorithm from the public key */
	pubkey = fu_jcat_gnutls_load_pubkey_from_privkey(key, error);
	if (pubkey == NULL)
		return NULL;
	rc = gnutls_pubkey_get_preferred_hash_algorithm(pubkey, &dig, NULL);
	if (rc < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "preferred_hash_algorithm: %s [%i]",
			    gnutls_strerror(rc),
			    rc);
		return NULL;
	}

	/* create container */
	rc = gnutls_mldsa87_init(&mldsa87);
	if (rc < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "mldsa87_init: %s [%i]",
			    gnutls_strerror(rc),
			    rc);
		return NULL;
	}

	/* sign data */
	d.data = (unsigned char *)g_bytes_get_data(blob, NULL);
	d.size = g_bytes_get_size(blob);
	if (flags & FU_JCAT_SIGN_FLAG_ADD_TIMESTAMP)
		gnutls_flags |= GNUTLS_MLDSA87_INCLUDE_TIME;
	if (flags & FU_JCAT_SIGN_FLAG_ADD_CERT)
		gnutls_flags |= GNUTLS_MLDSA87_INCLUDE_CERT;
	rc = gnutls_mldsa87_sign(mldsa87, crt, key, &d, NULL, NULL, dig, gnutls_flags);
	if (rc < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "mldsa87_sign: %s [%i]",
			    gnutls_strerror(rc),
			    rc);
		return NULL;
	}

	/* set certificate */
	if (flags & FU_JCAT_SIGN_FLAG_ADD_CERT) {
		rc = gnutls_mldsa87_set_crt(mldsa87, crt);
		if (rc < 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "mldsa87_set_cr: %s",
				    gnutls_strerror(rc));
			return NULL;
		}
	}

	/* export */
	rc = gnutls_mldsa87_export2(mldsa87, GNUTLS_X509_FMT_PEM, &d);
	if (rc < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "mldsa87_export: %s",
			    gnutls_strerror(rc));
		return NULL;
	}
	d_payload = d.data;
	str = g_strndup((const gchar *)d_payload, d.size);
	return fwupd_jcat_blob_new_utf8(FWUPD_JCAT_BLOB_KIND_PKCS7_MLDSA87, str);
}

/* creates a detached signature just like:
 *  `certtool --p7-detached-sign --load-certificate client.pem \
 *    --load-privkey secret.pem --outfile=test.p7b` */
static FwupdJcatBlob *
fu_jcat_gnutls_mldsa87_engine_self_sign(FuJcatEngine *engine,
					GBytes *blob,
					FuJcatSignFlags flags,
					GError **error)
{
	g_autofree gchar *fn_cert = NULL;
	g_autofree gchar *fn_privkey = NULL;
	g_autoptr(GBytes) cert = NULL;
	g_autoptr(GBytes) privkey = NULL;

	/* check private key exists, otherwise generate and save */
	fn_privkey =
	    g_build_filename(fu_jcat_engine_get_keyring_path(engine), "pki", "secret.key", NULL);
	if (g_file_test(fn_privkey, G_FILE_TEST_EXISTS)) {
		privkey = fu_bytes_get_contents(fn_privkey, error);
		if (privkey == NULL)
			return NULL;
	} else {
		privkey = fu_jcat_gnutls_create_private_key(GNUTLS_MLDSA87, error);
		if (privkey == NULL)
			return NULL;
		if (!fu_path_mkdir_parent(fn_privkey, error))
			return NULL;
		if (!fu_bytes_set_contents_full(fn_privkey, privkey, 0600, error))
			return NULL;
	}

	/* check client certificate exists, otherwise generate and save */
	fn_cert =
	    g_build_filename(fu_jcat_engine_get_keyring_path(engine), "pki", "client.pem", NULL);
	if (g_file_test(fn_cert, G_FILE_TEST_EXISTS)) {
		cert = fu_bytes_get_contents(fn_cert, error);
		if (cert == NULL)
			return NULL;
	} else {
		g_auto(gnutls_privkey_t) key = NULL;
		key = fu_jcat_gnutls_load_privkey_from_blob(privkey, error);
		if (key == NULL)
			return NULL;
		cert = fu_jcat_gnutls_create_client_certificate(key, error);
		if (cert == NULL)
			return NULL;
		if (!fu_path_mkdir_parent(fn_cert, error))
			return NULL;
		if (!fu_bytes_set_contents_full(fn_cert, cert, 0644, error))
			return NULL;
	}

	/* sign */
	return fu_jcat_gnutls_mldsa87_engine_pubkey_sign(engine, blob, cert, privkey, flags, error);
}

static void
fu_jcat_gnutls_mldsa87_engine_finalize(GObject *object)
{
	FuJcatGnutlsMldsa87Engine *self = FWUPD_JCAT_GNUTLS_MLDSA87_ENGINE(object);
	gnutls_x509_trust_list_deinit(self->tl, 1);
	G_OBJECT_CLASS(fu_jcat_gnutls_mldsa87_engine_parent_class)->finalize(object);
}

static void
fu_jcat_gnutls_mldsa87_engine_class_init(FuJcatGnutlsMldsa87EngineClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuJcatEngineClass *klass_app = FU_JCAT_ENGINE_CLASS(klass);
	klass_app->setup = fu_jcat_gnutls_mldsa87_engine_setup;
	klass_app->add_public_key = fu_jcat_gnutls_mldsa87_engine_add_public_key;
	klass_app->add_public_key_raw = fu_jcat_gnutls_mldsa87_engine_add_public_key_raw;
	klass_app->pubkey_verify = fu_jcat_gnutls_mldsa87_engine_pubkey_verify;
	klass_app->pubkey_sign = fu_jcat_gnutls_mldsa87_engine_pubkey_sign;
	klass_app->self_verify = fu_jcat_gnutls_mldsa87_engine_self_verify;
	klass_app->self_sign = fu_jcat_gnutls_mldsa87_engine_self_sign;
	object_class->finalize = fu_jcat_gnutls_mldsa87_engine_finalize;
}

static void
fu_jcat_gnutls_mldsa87_engine_init(FuJcatGnutlsMldsa87Engine *self)
{
}

FuJcatEngine *
fu_jcat_gnutls_mldsa87_engine_new(FuJcatContext *context)
{
	g_return_val_if_fail(FU_IS_JCAT_CONTEXT(context), NULL);
	return FU_JCAT_ENGINE(
	    g_object_new(FU_TYPE_JCAT_GNUTLS_MLDSA87_ENGINE,
			 "context",
			 context,
			 "kind",
			 FWUPD_JCAT_BLOB_KIND_PKCS7_MLDSA87,
			 "method",
			 FWUPD_JCAT_BLOB_METHOD_SIGNATURE,
			 NULL));
}
