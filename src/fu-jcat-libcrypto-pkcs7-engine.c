/*
 * Copyright (C) 2017-2020 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2025 Colin Kinloch <colin.kinloch@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-jcat-engine.h"
#include "fu-jcat-libcrypto-common.h"
#include "fu-jcat-libcrypto-pkcs7-engine.h"

struct _FuJcatLibcryptoPkcs7Engine {
	FuJcatEngine parent_instance;
	X509_STORE *trust_store;
};

G_DEFINE_TYPE(FuJcatLibcryptoPkcs7Engine, fu_jcat_libcrypto_pkcs7_engine, FU_TYPE_JCAT_ENGINE)

static gboolean
fu_jcat_libcrypto_pkcs7_engine_add_pubkey_x509(FuJcatLibcryptoPkcs7Engine *self,
					       X509 *crt,
					       GError **error)
{
	guint32 key_usage = X509_get_key_usage(crt);

	if ((key_usage & X509v3_KU_DIGITAL_SIGNATURE) == 0 &&
	    (key_usage & X509v3_KU_KEY_CERT_SIGN) == 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "certificate not suitable for use [0x%x]",
			    key_usage);
		return FALSE;
	}

	if (!X509_STORE_add_cert(self->trust_store, crt)) {
		g_autofree gchar *error_str = fu_jcat_libcrypto_get_errors();
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "failed to add to trust list: %s",
			    error_str);
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_jcat_libcrypto_pkcs7_engine_add_public_key_raw(FuJcatEngine *engine,
						  GBytes *blob,
						  GError **error)
{
	FuJcatLibcryptoPkcs7Engine *self = FU_JCAT_LIBCRYPTO_PKCS7_ENGINE(engine);
	g_autoptr(X509) cert = NULL;

	cert = fu_jcat_libcrypto_pkcs7_load_crt_from_blob_pem(blob, error);
	if (cert == NULL)
		return FALSE;
	return fu_jcat_libcrypto_pkcs7_engine_add_pubkey_x509(self, cert, error);
}

static gboolean
fu_jcat_libcrypto_pkcs7_engine_add_public_key(FuJcatEngine *engine,
					      const gchar *filename,
					      GError **error)
{
	FuJcatLibcryptoPkcs7Engine *self = FU_JCAT_LIBCRYPTO_PKCS7_ENGINE(engine);
	g_autoptr(X509) crt = NULL;

	/* search all the public key files */
	if (g_str_has_suffix(filename, ".pem") || g_str_has_suffix(filename, ".crt")) {
		g_autoptr(GBytes) blob = fu_bytes_get_contents(filename, error);
		if (blob == NULL)
			return FALSE;
		crt = fu_jcat_libcrypto_pkcs7_load_crt_from_blob_pem(blob, error);
		if (crt == NULL)
			return FALSE;
		if (!fu_jcat_libcrypto_pkcs7_engine_add_pubkey_x509(self, crt, error))
			return FALSE;
	} else if (g_str_has_suffix(filename, ".cer") || g_str_has_suffix(filename, ".der")) {
		g_autoptr(GBytes) blob = fu_bytes_get_contents(filename, error);
		if (blob == NULL)
			return FALSE;
		crt = fu_jcat_libcrypto_pkcs7_load_crt_from_blob_der(blob, error);
		if (crt == NULL)
			return FALSE;
		if (!fu_jcat_libcrypto_pkcs7_engine_add_pubkey_x509(self, crt, error))
			return FALSE;
	} else {
		g_autofree gchar *basename = g_path_get_basename(filename);
		g_debug("ignoring %s as not PKCS-7 certificate", basename);
	}

	return TRUE;
}

static gboolean
fu_jcat_libcrypto_pkcs7_engine_setup(FuJcatEngine *engine, GError **error)
{
	FuJcatLibcryptoPkcs7Engine *self = FU_JCAT_LIBCRYPTO_PKCS7_ENGINE(engine);

	if (self->trust_store != NULL)
		return TRUE;

	self->trust_store = X509_STORE_new();
	if (self->trust_store == NULL) {
		g_autofree gchar *error_str = fu_jcat_libcrypto_get_errors();
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "failed to create trust list: %s",
			    error_str);
		return FALSE;
	}

	return TRUE;
}

/* nocheck:name */
static int
cms_verify_cb(int ok, X509_STORE_CTX *ctx)
{
	/* on error store the name of the current issuer */
	if (ok == 0) {
		X509 *crt = X509_STORE_CTX_get_current_cert(ctx);
		X509_STORE *store = X509_STORE_CTX_get0_store(ctx);
		g_autofree gchar *issuer_name = fu_jcat_libcrypto_x509_get_issuer_name(crt, NULL);
		g_free(X509_STORE_get_ex_data(store, 0));
		X509_STORE_set_ex_data(store, 0, g_steal_pointer(&issuer_name));
	}

	/* don't override errors */
	return ok;
}

/* verifies a detached signature just like:
 *  `certtool --p7-verify --load-certificate client.pem --infile=test.p7b` */
static FuJcatResult *
fu_jcat_libcrypto_pkcs7_engine_verify(FuJcatEngine *engine,
				      GBytes *blob,
				      GBytes *blob_signature,
				      X509 *crt,
				      FuJcatVerifyFlags flags,
				      GError **error)
{
	FuJcatLibcryptoPkcs7Engine *self = FU_JCAT_LIBCRYPTO_PKCS7_ENGINE(engine);
	gsize blob_size = 0;
	gsize sig_size = 0;
	int verify_flags = CMS_BINARY;
	gint64 timestamp_newest = 0;
	g_autoptr(BIO) bio = NULL;
	g_autoptr(BIO) bio_signature = NULL;
	g_autoptr(CMS_ContentInfo) cms = NULL;
	g_autoptr(GString) authority_newest = g_string_new(NULL);
	g_autoptr(X509_STORE) self_store = NULL;
	g_autoptr(STACK_OF_X509) signer_certs = NULL;
	X509_STORE *trust_store = NULL;
	STACK_OF(CMS_SignerInfo) *infos = NULL;

	/* import the signature */
	sig_size = g_bytes_get_size(blob_signature);
	if (sig_size > G_MAXINT) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "signature data too large: %" G_GSIZE_FORMAT,
			    sig_size);
		return NULL;
	}
	bio_signature = BIO_new_mem_buf(g_bytes_get_data(blob_signature, NULL), (int)sig_size);
	cms = PEM_read_bio_CMS(bio_signature, &cms, NULL, NULL);
	if (cms == NULL) {
		g_autofree gchar *error_str = fu_jcat_libcrypto_get_errors();
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "failed to parse PKCS7 signature: %s",
			    error_str);
		return NULL;
	}

	if (crt == NULL) {
		trust_store = self->trust_store;
	} else {
		/* setup structures for self-signed signature */
		self_store = X509_STORE_new();
		if (!X509_STORE_add_cert(self_store, crt)) {
			g_autofree gchar *error_str = fu_jcat_libcrypto_get_errors();
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "failed to add certificate to store: %s",
				    error_str);
			return NULL;
		}
		signer_certs = sk_X509_new_null();
		sk_X509_push(signer_certs, crt);
		trust_store = self_store;
	}

	/* configure trust store for verification */
	{
		X509_VERIFY_PARAM *param = X509_STORE_get0_param(trust_store);
		/* without setting this the LVFS-CA.pem can't be used to for verify CMS */
		X509_VERIFY_PARAM_set_purpose(param, X509_PURPOSE_ANY);

		/* use with care */
		if (flags & FU_JCAT_VERIFY_FLAG_DISABLE_TIME_CHECKS) {
			g_debug("WARNING: disabling time checks");
			X509_VERIFY_PARAM_set_flags(param, X509_V_FLAG_NO_CHECK_TIME);
		} else {
			X509_VERIFY_PARAM_clear_flags(param, X509_V_FLAG_NO_CHECK_TIME);
		}
	}

	/* setup verify callback for debugging */
	X509_STORE_set_ex_data(trust_store, 0, NULL);
	X509_STORE_set_verify_cb(trust_store, cms_verify_cb);

	/* verify the blob */
	blob_size = g_bytes_get_size(blob);
	if (blob_size > G_MAXINT) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "blob data too large: %" G_GSIZE_FORMAT,
			    blob_size);
		return NULL;
	}
	bio = BIO_new_mem_buf(g_bytes_get_data(blob, NULL), (int)blob_size);
	infos = CMS_get0_SignerInfos(cms);
	g_debug("got %i PKCS7 signatures", sk_CMS_SignerInfo_num(infos));
	if (sk_CMS_SignerInfo_num(infos) == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "no PKCS7 signatures found");
		return NULL;
	}
	if (CMS_verify(cms, signer_certs, trust_store, bio, NULL, verify_flags) <= 0) {
		g_autofree gchar *error_str = fu_jcat_libcrypto_get_errors();
		g_autofree gchar *issuer_name = X509_STORE_get_ex_data(trust_store, 0);
		if (issuer_name != NULL) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "failed to verify data for %s: %s",
				    issuer_name,
				    error_str);
			return NULL;
		}
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "failed to verify data: %s",
			    error_str);
		return NULL;
	}

	/* save details about the key for the result */
	for (int i = 0; i < sk_CMS_SignerInfo_num(infos); i++) {
		CMS_SignerInfo *info = sk_CMS_SignerInfo_value(infos, i);
		g_autoptr(BIO) time_bio = NULL;
		struct tm time_tm;
		time_t signing_time = 0;
		int stime_loc = CMS_signed_get_attr_by_NID(info, NID_pkcs9_signingTime, -1);
		X509_ATTRIBUTE *stime_attr = NULL;
		ASN1_TYPE *stime = NULL;
		ASN1_TIME *t = NULL;
		g_autoptr(BIO) issuer_bio = BIO_new(BIO_s_mem());

		BIO_set_flags(issuer_bio, BIO_FLAGS_NONCLEAR_RST);

		if (stime_loc >= 0)
			stime_attr = CMS_signed_get_attr(info, stime_loc);
		if (stime_attr != NULL)
			stime = X509_ATTRIBUTE_get0_type(stime_attr, 0);
		if (stime != NULL) {
			if (stime->type == V_ASN1_UTCTIME ||
			    stime->type == V_ASN1_GENERALIZEDTIME) {
				t = (ASN1_TIME *)stime->value.asn1_value;
			} else {
				g_autofree gchar *error_str = fu_jcat_libcrypto_get_errors();
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "failed to extract timestamp: %s",
					    error_str);
				return NULL;
			}
		}

		if (t != NULL && !ASN1_TIME_to_tm(t, &time_tm)) {
			g_autofree gchar *error_str = fu_jcat_libcrypto_get_errors();
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "failed to convert timestamp: %s",
				    error_str);
			return NULL;
		}

		if (t != NULL) {
			signing_time = timegm(&time_tm);
			if (signing_time == (time_t)-1) {
				g_set_error_literal(error,
						    FWUPD_ERROR,
						    FWUPD_ERROR_INVALID_DATA,
						    "failed to convert signing time");
				return NULL;
			}
		}
		if (signing_time > timestamp_newest || timestamp_newest == 0) {
			X509_NAME *issuer_name = NULL;
			gchar *issuer_string = NULL;

			timestamp_newest = signing_time;

			if (!CMS_SignerInfo_get0_signer_id(info, NULL, &issuer_name, NULL)) {
				g_autofree gchar *error_str = fu_jcat_libcrypto_get_errors();
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "failed to extract issuer name: %s",
					    error_str);
				return NULL;
			}

			BIO_reset(issuer_bio);
			if (X509_NAME_print_ex(issuer_bio, issuer_name, 0, XN_FLAG_RFC2253) == -1) {
				g_autofree gchar *error_str = fu_jcat_libcrypto_get_errors();
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "failed to print issuer name: %s",
					    error_str);
				return NULL;
			}

			/* NULL terminate the BIO */
			BIO_write(issuer_bio, "", 1);
			BIO_get_mem_data(issuer_bio, &issuer_string);
			g_string_assign(authority_newest, issuer_string);
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

static FuJcatResult *
fu_jcat_libcrypto_pkcs7_engine_self_verify(FuJcatEngine *engine,
					   GBytes *blob,
					   GBytes *blob_signature,
					   FuJcatVerifyFlags flags,
					   GError **error)
{
	g_autofree gchar *filename = NULL;
	g_autoptr(GBytes) cert_blob = NULL;
	g_autoptr(X509) crt = NULL;

	filename =
	    g_build_filename(fu_jcat_engine_get_keyring_path(engine), "pki", "client.pem", NULL);
	cert_blob = fu_bytes_get_contents(filename, error);
	if (cert_blob == NULL)
		return NULL;
	crt = fu_jcat_libcrypto_pkcs7_load_crt_from_blob_pem(cert_blob, error);
	if (crt == NULL)
		return NULL;

	return fu_jcat_libcrypto_pkcs7_engine_verify(engine,
						     blob,
						     blob_signature,
						     crt,
						     flags,
						     error);
}

static FuJcatResult *
fu_jcat_libcrypto_pkcs7_engine_pubkey_verify(FuJcatEngine *engine,
					     GBytes *blob,
					     GBytes *blob_signature,
					     FuJcatVerifyFlags flags,
					     GError **error)
{
	return fu_jcat_libcrypto_pkcs7_engine_verify(engine,
						     blob,
						     blob_signature,
						     NULL,
						     flags,
						     error);
}

static FwupdJcatBlob *
fu_jcat_libcrypto_pkcs7_engine_pubkey_sign(FuJcatEngine *engine,
					   GBytes *blob,
					   GBytes *cert,
					   GBytes *privkey,
					   FuJcatSignFlags flags,
					   GError **error)
{
	gchar *bio_buf;
	guint signing_flags = CMS_BINARY | CMS_DETACHED | CMS_NOSMIMECAP;
	g_autoptr(BIO) blob_bio = NULL;
	g_autoptr(BIO) sig_bio = NULL;
	g_autoptr(CMS_ContentInfo) cms = NULL;
	g_autoptr(EVP_PKEY) key = NULL;
	g_autoptr(X509) crt = NULL;

	/* nothing to do */
	if (g_bytes_get_size(blob) == 0) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOTHING_TO_DO, "nothing to do");
		return NULL;
	}

	/* load keys */
	key = fu_jcat_libcrypto_pkcs7_load_privkey_from_blob_pem(privkey, error);
	if (key == NULL)
		return NULL;
	crt = fu_jcat_libcrypto_pkcs7_load_crt_from_blob_pem(cert, error);
	if (crt == NULL)
		return NULL;

	/* signing is done using the default for the supplied key (SHA256 for RSA)
	 * if a different digest algorithm is needed CMS_add1_signer can be used */

	/* sign data */
	if (!(flags & FU_JCAT_SIGN_FLAG_ADD_TIMESTAMP)) {
#if OPENSSL_VERSION_PREREQ(3, 5)
		signing_flags |= CMS_NO_SIGNING_TIME;
#else
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "signing without a timestamp requires OpenSSL 3.5 or later");
		return NULL;
#endif
	}

	if (!(flags & FU_JCAT_SIGN_FLAG_ADD_CERT))
		signing_flags |= CMS_NOCERTS;
	if (g_bytes_get_size(blob) > G_MAXINT) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "blob data too large: %" G_GSIZE_FORMAT,
			    g_bytes_get_size(blob));
		return NULL;
	}
	blob_bio = BIO_new_mem_buf(g_bytes_get_data(blob, NULL), (int)g_bytes_get_size(blob));
	cms = CMS_sign(crt, key, NULL, blob_bio, signing_flags);
	if (cms == NULL) {
		g_autofree gchar *error_str = fu_jcat_libcrypto_get_errors();
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "failed to create CMS signature: %s",
			    error_str);
		return NULL;
	}

	sig_bio = BIO_new(BIO_s_mem());

	/* PEM_write_bio_CMS is a wrapper of PEM_ASN1_write_bio, by setting PEM_STRING_PKCS7 we
	 * create get a PEM representation with a PKCS7 header */
	if (!PEM_ASN1_write_bio((i2d_of_void *)i2d_CMS_ContentInfo,
				PEM_STRING_PKCS7,
				sig_bio,
				cms,
				NULL,
				NULL,
				0,
				NULL,
				NULL)) {
		g_autofree gchar *error_str = fu_jcat_libcrypto_get_errors();
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "failed to encode PKCS7: %s",
			    error_str);
		return NULL;
	}

	/* NULL terminate the BIO */
	BIO_write(sig_bio, "", 1);
	BIO_get_mem_data(sig_bio, &bio_buf);

	return fwupd_jcat_blob_new_utf8(FWUPD_JCAT_BLOB_KIND_PKCS7, bio_buf);
}

static FwupdJcatBlob *
fu_jcat_libcrypto_pkcs7_engine_self_sign(FuJcatEngine *engine,
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
		privkey = fu_jcat_libcrypto_pkcs7_create_private_key(error);
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
		g_autoptr(EVP_PKEY) key = NULL;
		key = fu_jcat_libcrypto_pkcs7_load_privkey_from_blob_pem(privkey, error);
		if (key == NULL)
			return NULL;
		cert = fu_jcat_libcrypto_pkcs7_create_client_certificate(key, error);
		if (cert == NULL)
			return NULL;
		if (!fu_path_mkdir_parent(fn_cert, error))
			return NULL;
		if (!fu_bytes_set_contents_full(fn_cert, cert, 0644, error))
			return NULL;
	}

	/* sign */
	return fu_jcat_libcrypto_pkcs7_engine_pubkey_sign(engine,
							  blob,
							  cert,
							  privkey,
							  flags,
							  error);
}

static void
fu_jcat_libcrypto_pkcs7_engine_finalize(GObject *object)
{
	FuJcatLibcryptoPkcs7Engine *self = FU_JCAT_LIBCRYPTO_PKCS7_ENGINE(object);

	X509_STORE_free(self->trust_store);

	G_OBJECT_CLASS(fu_jcat_libcrypto_pkcs7_engine_parent_class)->finalize(object);
}

static void
fu_jcat_libcrypto_pkcs7_engine_class_init(FuJcatLibcryptoPkcs7EngineClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuJcatEngineClass *engine_class = FU_JCAT_ENGINE_CLASS(klass);
	engine_class->setup = fu_jcat_libcrypto_pkcs7_engine_setup;
	engine_class->add_public_key = fu_jcat_libcrypto_pkcs7_engine_add_public_key;
	engine_class->add_public_key_raw = fu_jcat_libcrypto_pkcs7_engine_add_public_key_raw;
	engine_class->pubkey_verify = fu_jcat_libcrypto_pkcs7_engine_pubkey_verify;
	engine_class->pubkey_sign = fu_jcat_libcrypto_pkcs7_engine_pubkey_sign;
	engine_class->self_verify = fu_jcat_libcrypto_pkcs7_engine_self_verify;
	engine_class->self_sign = fu_jcat_libcrypto_pkcs7_engine_self_sign;
	object_class->finalize = fu_jcat_libcrypto_pkcs7_engine_finalize;
}

static void
fu_jcat_libcrypto_pkcs7_engine_init(FuJcatLibcryptoPkcs7Engine *self)
{
}

FuJcatEngine *
fu_jcat_libcrypto_pkcs7_engine_new(FuJcatContext *context)
{
	g_return_val_if_fail(FU_IS_JCAT_CONTEXT(context), NULL);
	return FU_JCAT_ENGINE(g_object_new(FU_TYPE_JCAT_LIBCRYPTO_PKCS7_ENGINE,
					   "context",
					   context,
					   "kind",
					   FWUPD_JCAT_BLOB_KIND_PKCS7,
					   "method",
					   FWUPD_JCAT_BLOB_METHOD_SIGNATURE,
					   NULL));
}
