/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2025 Colin Kinloch <colin.kinloch@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-jcat-libcrypto-common.h"

#define FU_JCAT_LIBCRYPTO_RSA_SIZE 3072
/* To indicate that a certificate has no well-defined expiration date:
 * https://www.rfc-editor.org/rfc/rfc5280#section-4.1.2.5 */
#define FU_JCAT_LIBCRYPTO_X509_NOTAFTER_UNDEFINED "99991231235959Z"

/* nocheck:name */
void
STACK_OF_X509_free(STACK_OF_X509 *stack)
{
	return sk_X509_free(stack);
}

gchar *
fu_jcat_libcrypto_get_errors(void)
{
	unsigned long packed_error = ERR_peek_error();
	size_t bio_len = 0;
	char *bio_buf = NULL;
	g_autoptr(BIO) string_bio = BIO_new(BIO_s_mem());

	/* nothing found */
	if (packed_error == 0)
		return g_strdup("unspecified error");
	if (string_bio == NULL)
		return g_strdup("unspecified error");

	ERR_print_errors(string_bio);
	bio_len = BIO_get_mem_data(string_bio, &bio_buf);
	return g_strndup(bio_buf, bio_len);
}

X509 *
fu_jcat_libcrypto_pkcs7_load_crt_from_blob_pem(GBytes *blob, GError **error)
{
	g_autoptr(BIO) bio_blob = NULL;
	g_autoptr(X509) crt = NULL;
	gsize blob_size = g_bytes_get_size(blob);

	if (blob_size > G_MAXINT) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "cert PEM data too large: %" G_GSIZE_FORMAT,
			    blob_size);
		return NULL;
	}

	/* discard OpenSSL warning about SHA1 use from this function */
	/* PEM_read_bio_X509 succeeds but pushes rh-allow-sha1-signatures to the error stack */
	ERR_set_mark();

	bio_blob = BIO_new_mem_buf(g_bytes_get_data(blob, NULL), (int)blob_size);
	if (bio_blob == NULL) {
		g_autofree gchar *error_str = fu_jcat_libcrypto_get_errors();
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "failed to create buffer for cert PEM data: %s",
			    error_str);
		return NULL;
	}

	/* decode the certificate */
	if (!PEM_read_bio_X509(bio_blob, &crt, NULL, NULL)) {
		g_autofree gchar *error_str = fu_jcat_libcrypto_get_errors();
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "failed to parse certificate from PEM buffer: %s",
			    error_str);
		return NULL;
	}

	ERR_pop_to_mark();

	return g_steal_pointer(&crt);
}

X509 *
fu_jcat_libcrypto_pkcs7_load_crt_from_blob_der(GBytes *blob, GError **error)
{
	g_autoptr(X509) crt = NULL;
	gsize blob_size = g_bytes_get_size(blob);
	const guchar *blob_data = g_bytes_get_data(blob, NULL);

	if (blob_size > G_MAXLONG) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "cert DER data too large: %" G_GSIZE_FORMAT,
			    blob_size);
		return NULL;
	}

	/* discard OpenSSL warning about SHA1 use from this function */
	ERR_set_mark();

	/* decode the certificate */
	if (!d2i_X509(&crt, &blob_data, (long)blob_size)) {
		g_autofree gchar *error_str = fu_jcat_libcrypto_get_errors();
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "failed to parse certificate from DER buffer: %s",
			    error_str);
		return NULL;
	}

	ERR_pop_to_mark();

	return g_steal_pointer(&crt);
}

EVP_PKEY *
fu_jcat_libcrypto_pkcs7_load_privkey_from_blob_pem(GBytes *blob, GError **error)
{
	gsize blob_size;
	const guchar *blob_data = g_bytes_get_data(blob, &blob_size);
	g_autoptr(EVP_PKEY) key = NULL;
	g_autoptr(OSSL_DECODER_CTX) dec_ctx = NULL;

	dec_ctx = OSSL_DECODER_CTX_new_for_pkey(&key,
						"PEM",
						NULL,
						NULL,
						OSSL_KEYMGMT_SELECT_KEYPAIR,
						NULL,
						NULL);
	if (dec_ctx == NULL) {
		g_autofree gchar *error_str = fu_jcat_libcrypto_get_errors();
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "failed setup keypair decoder: %s",
			    error_str);
		return NULL;
	}

	if (!OSSL_DECODER_from_data(dec_ctx, &blob_data, &blob_size)) {
		g_autofree gchar *error_str = fu_jcat_libcrypto_get_errors();
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "failed to decode keypair: %s",
			    error_str);
		return NULL;
	}

	return g_steal_pointer(&key);
}

GBytes *
fu_jcat_libcrypto_pkcs7_create_private_key(GError **error)
{
	gsize pem_data_size = 0;
	g_autofree guchar *pem_data = NULL;
	g_autoptr(EVP_PKEY_CTX) ctx = NULL;
	g_autoptr(EVP_PKEY) pkey = NULL;
	g_autoptr(OSSL_ENCODER_CTX) enc_ctx = NULL;

	ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL);
	if (ctx == NULL) {
		g_autofree gchar *error_str = fu_jcat_libcrypto_get_errors();
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "failed to create RSA keygen: %s",
			    error_str);
		return NULL;
	}

	if (EVP_PKEY_keygen_init(ctx) <= 0) {
		g_autofree gchar *error_str = fu_jcat_libcrypto_get_errors();
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "failed to init RSA keygen: %s",
			    error_str);
		return NULL;
	}

	if (EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, FU_JCAT_LIBCRYPTO_RSA_SIZE) <= 0) {
		g_autofree gchar *error_str = fu_jcat_libcrypto_get_errors();
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "failed to set RSA keygen bits: %s",
			    error_str);
		return NULL;
	}

	/* generate key */
	if (EVP_PKEY_keygen(ctx, &pkey) <= 0) {
		g_autofree gchar *error_str = fu_jcat_libcrypto_get_errors();
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "failed to generate RSA key: %s",
			    error_str);
		return NULL;
	}

	enc_ctx = OSSL_ENCODER_CTX_new_for_pkey(pkey, EVP_PKEY_KEYPAIR, "PEM", NULL, NULL);
	if (enc_ctx == NULL) {
		g_autofree gchar *error_str = fu_jcat_libcrypto_get_errors();
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "failed to create an encoder for RSA keypair: %s",
			    error_str);
		return NULL;
	}

	if (OSSL_ENCODER_to_data(enc_ctx, &pem_data, &pem_data_size) == 0) {
		g_autofree gchar *error_str = fu_jcat_libcrypto_get_errors();
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "failed to encode RSA keypair: %s",
			    error_str);
		return NULL;
	}

	return g_bytes_new_take(g_steal_pointer(&pem_data), pem_data_size);
}

GBytes *
fu_jcat_libcrypto_pkcs7_create_client_certificate(EVP_PKEY *privkey, GError **error)
{
	const guchar *crt_buf;
	gsize crt_size;
	guchar md[EVP_MAX_MD_SIZE] = {0};
	guint md_size = 0;
	g_autoptr(ASN1_BIT_STRING) usage = ASN1_BIT_STRING_new();
	g_autoptr(ASN1_OCTET_STRING) skid = ASN1_OCTET_STRING_new();
	g_autoptr(BASIC_CONSTRAINTS) bcons = BASIC_CONSTRAINTS_new();
	g_autoptr(BIGNUM) sha1bn = BN_new();
	g_autoptr(BIO) crt_bio = BIO_new(BIO_s_mem());
	g_autoptr(X509) crt = X509_new();

	/* set public key */
	if (!X509_set_pubkey(crt, privkey)) {
		g_autofree gchar *error_str = fu_jcat_libcrypto_get_errors();
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "failed to set client cert public key: %s",
			    error_str);
		return NULL;
	}

	/* generate random number 1 bit short of 20 bytes so that it's MSb is 0 (it's positive)
	 * This matches the OpenSSL implementation */
	if (!BN_rand(sha1bn, (20 * 8) - 1, BN_RAND_TOP_ANY, BN_RAND_BOTTOM_ANY)) {
		g_autofree gchar *error_str = fu_jcat_libcrypto_get_errors();
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "failed to generate cert serial number: %s",
			    error_str);
		return NULL;
	}
	if (!BN_to_ASN1_INTEGER(sha1bn, X509_get_serialNumber(crt))) {
		g_autofree gchar *error_str = fu_jcat_libcrypto_get_errors();
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "failed to set serial number: %s",
			    error_str);
		return NULL;
	}

	/* set activation */
	if (!ASN1_TIME_set(X509_getm_notBefore(crt), time(NULL))) {
		g_autofree gchar *error_str = fu_jcat_libcrypto_get_errors();
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "failed to set client cert activation time: %s",
			    error_str);
		return NULL;
	}

	/* set expiration */
	if (!ASN1_TIME_set_string(X509_getm_notAfter(crt),
				  FU_JCAT_LIBCRYPTO_X509_NOTAFTER_UNDEFINED)) {
		g_autofree gchar *error_str = fu_jcat_libcrypto_get_errors();
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "failed to set client cert expiration time: %s",
			    error_str);
		return NULL;
	}

	/* set basic constraints */
	bcons->ca = FALSE;
	if (X509_add1_ext_i2d(crt, NID_basic_constraints, bcons, 1, X509V3_ADD_DEFAULT) < 1) {
		g_autofree gchar *error_str = fu_jcat_libcrypto_get_errors();
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "failed to add basic constraints to cert: %s",
			    error_str);
		return NULL;
	}

	/* set usage */
	ASN1_BIT_STRING_set_bit(usage, 0, 1);
	if (X509_add1_ext_i2d(crt, NID_key_usage, usage, 1, X509V3_ADD_DEFAULT) < 1) {
		g_autofree gchar *error_str = fu_jcat_libcrypto_get_errors();
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "failed to add key usage to cert: %s",
			    error_str);
		return NULL;
	}

	/* set subject key ID */
	if (!X509_pubkey_digest(crt, EVP_sha1(), md, &md_size)) {
		g_autofree gchar *error_str = fu_jcat_libcrypto_get_errors();
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "failed to create digest of pubkey for subject key ID: %s",
			    error_str);
		return NULL;
	}
	ASN1_OCTET_STRING_set(skid, md, md_size);
	if (X509_add1_ext_i2d(crt, NID_subject_key_identifier, skid, 0, X509V3_ADD_DEFAULT) < 1) {
		g_autofree gchar *error_str = fu_jcat_libcrypto_get_errors();
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "failed to add subject key ID to cert: %s",
			    error_str);
		return NULL;
	}

	/* set version */
	if (!X509_set_version(crt, X509_VERSION_3)) {
		g_autofree gchar *error_str = fu_jcat_libcrypto_get_errors();
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "failed to set cert version number: %s",
			    error_str);
		return NULL;
	}

	/* self-sign certificate */
	if (!X509_sign(crt, privkey, EVP_sha256())) {
		g_autofree gchar *error_str = fu_jcat_libcrypto_get_errors();
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "failed to self sign cert: %s",
			    error_str);
		return NULL;
	}

	/* encode as PEM */
	if (!PEM_write_bio_X509(crt_bio, crt)) {
		g_autofree gchar *error_str = fu_jcat_libcrypto_get_errors();
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "failed to encode self-signing cert: %s",
			    error_str);
		return NULL;
	}

	crt_size = BIO_get_mem_data(crt_bio, &crt_buf);
	return g_bytes_new(crt_buf, crt_size);
}

gchar *
fu_jcat_libcrypto_x509_get_issuer_name(X509 *crt, GError **error)
{
	g_autoptr(BIO) issuer_bio = BIO_new(BIO_s_mem());
	X509_NAME *issuer_name = X509_get_issuer_name(crt);
	gchar *issuer_string = NULL;
	gsize issuer_string_length;

	if (X509_NAME_print_ex(issuer_bio, issuer_name, 0, XN_FLAG_RFC2253) == -1) {
		g_autofree gchar *error_str = fu_jcat_libcrypto_get_errors();
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "failed to get issuer name: %s",
			    error_str);
		return NULL;
	}

	issuer_string_length = BIO_get_mem_data(issuer_bio, &issuer_string);
	return g_strndup(issuer_string, issuer_string_length);
}
