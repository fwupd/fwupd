/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-jcat-gnutls-common.h"

gboolean
fu_jcat_gnutls_ensure_trust_list_valid(gnutls_x509_trust_list_t tl, GError **error)
{
	guint cnt = 0;
	g_auto(gnutls_x509_trust_list_iter_t) iter = NULL;

	/* check we have more than zero certs */
	while (1) {
		int rc;
		g_auto(gnutls_x509_crt_t) cert = NULL;

		rc = gnutls_x509_trust_list_iter_get_ca(tl, &iter, &cert);
		if (rc == GNUTLS_E_REQUESTED_DATA_NOT_AVAILABLE)
			break;
		if (rc != GNUTLS_E_SUCCESS) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "failed to get ca from trust list: %s [%i]",
				    gnutls_strerror(rc),
				    rc);
			return FALSE;
		}
		cnt++;
	}
	if (cnt == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "no certificates in trust list");
		return FALSE;
	}

	/* success */
	return TRUE;
}

gboolean
fu_jcat_gnutls_datum_from_bytes(gnutls_datum_t *d, GBytes *blob, GError **error)
{
	if (g_bytes_get_size(blob) > G_MAXUINT) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA, "blob too large");
		return FALSE;
	}
	d->size = (unsigned int)g_bytes_get_size(blob);
	d->data = (unsigned char *)g_bytes_get_data(blob, NULL);
	return TRUE;
}

gnutls_x509_crt_t
fu_jcat_gnutls_pkcs7_load_crt_from_blob(GBytes *blob, gnutls_x509_crt_fmt_t format, GError **error)
{
	gnutls_datum_t d = {0};
	int rc;
	g_auto(gnutls_x509_crt_t) crt = NULL;

	/* create certificate */
	rc = gnutls_x509_crt_init(&crt);
	if (!fu_jcat_gnutls_rc_to_error(rc, error)) {
		g_prefix_error_literal(error, "failed to crt_init: ");
		return NULL;
	}

	/* import the certificate */
	if (!fu_jcat_gnutls_datum_from_bytes(&d, blob, error))
		return NULL;
	rc = gnutls_x509_crt_import(crt, &d, format);
	if (!fu_jcat_gnutls_rc_to_error(rc, error)) {
		g_prefix_error_literal(error, "failed to crt_import: ");
		return NULL;
	}
	return g_steal_pointer(&crt);
}

gnutls_privkey_t
fu_jcat_gnutls_pkcs7_load_privkey_from_blob(GBytes *blob, GError **error)
{
	int rc;
	gnutls_datum_t d = {0};
	g_auto(gnutls_privkey_t) key = NULL;

	/* load the private key */
	rc = gnutls_privkey_init(&key);
	if (!fu_jcat_gnutls_rc_to_error(rc, error)) {
		g_prefix_error_literal(error, "failed to privkey_init: ");
		return NULL;
	}
	if (!fu_jcat_gnutls_datum_from_bytes(&d, blob, error))
		return NULL;
	rc = gnutls_privkey_import_x509_raw(key, &d, GNUTLS_X509_FMT_PEM, NULL, 0);
	if (!fu_jcat_gnutls_rc_to_error(rc, error)) {
		g_prefix_error_literal(error, "failed to privkey_import_x509_raw: ");
		return NULL;
	}
	return g_steal_pointer(&key);
}

gnutls_pubkey_t
fu_jcat_gnutls_pkcs7_load_pubkey_from_privkey(gnutls_privkey_t privkey, GError **error)
{
	g_auto(gnutls_pubkey_t) pubkey = NULL;
	int rc;

	/* get the public key part of the private key */
	rc = gnutls_pubkey_init(&pubkey);
	if (!fu_jcat_gnutls_rc_to_error(rc, error)) {
		g_prefix_error_literal(error, "failed to pubkey_init: ");
		return NULL;
	}
	rc = gnutls_pubkey_import_privkey(pubkey, privkey, 0, 0);
	if (!fu_jcat_gnutls_rc_to_error(rc, error)) {
		g_prefix_error_literal(error, "failed to pubkey_import_privkey: ");
		return NULL;
	}

	/* success */
	return g_steal_pointer(&pubkey);
}

gchar *
fu_jcat_gnutls_pkcs7_datum_to_dn_str(const gnutls_datum_t *raw)
{
	g_auto(gnutls_x509_dn_t) dn = NULL;
	g_autoptr(gnutls_datum_t) str = NULL;
	int rc;
	rc = gnutls_x509_dn_init(&dn);
	if (rc < 0)
		return NULL;
	rc = gnutls_x509_dn_import(dn, raw);
	if (rc < 0)
		return NULL;
	str = (gnutls_datum_t *)gnutls_calloc(1, sizeof(gnutls_datum_t));
	if (str == NULL)
		return NULL;
	str->data = NULL;
	rc = gnutls_x509_dn_get_str2(dn, str, 0);
	if (rc < 0)
		return NULL;
	return g_strndup((const gchar *)str->data, str->size);
}

/* generates a private key just like `certtool --generate-privkey` */
GBytes *
fu_jcat_gnutls_pkcs7_create_private_key(gnutls_pk_algorithm_t algo, GError **error)
{
	gnutls_datum_t d = {0};
	int bits;
	int rc;
	g_auto(gnutls_x509_privkey_t) key = NULL;
	g_autoptr(gnutls_data_t) d_payload = NULL;

	/* initialize key and SPKI */
	rc = gnutls_x509_privkey_init(&key);
	if (!fu_jcat_gnutls_rc_to_error(rc, error)) {
		g_prefix_error_literal(error, "failed to privkey_init: ");
		return NULL;
	}

	/* generate key */
	bits = gnutls_sec_param_to_pk_bits(algo, GNUTLS_SEC_PARAM_HIGH);
	if (bits == 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "failed to get key size for %s",
			    gnutls_pk_algorithm_get_name(algo));
		return NULL;
	}
	g_debug("generating a %d bit %s private key", bits, gnutls_pk_algorithm_get_name(algo));
	rc = gnutls_x509_privkey_generate2(key, algo, bits, 0, NULL, 0);
	if (!fu_jcat_gnutls_rc_to_error(rc, error)) {
		g_prefix_error_literal(error, "failed to privkey_generate2: ");
		return NULL;
	}
	rc = gnutls_x509_privkey_verify_params(key);
	if (!fu_jcat_gnutls_rc_to_error(rc, error)) {
		g_prefix_error_literal(error, "failed to privkey_verify_params: ");
		return NULL;
	}

	/* save to file */
	rc = gnutls_x509_privkey_export2(key, GNUTLS_X509_FMT_PEM, &d);
	if (!fu_jcat_gnutls_rc_to_error(rc, error)) {
		g_prefix_error_literal(error, "failed to privkey_export2: ");
		return NULL;
	}
	d_payload = d.data;
	return g_bytes_new(d_payload, d.size);
}

gboolean
fu_jcat_gnutls_pkcs7_ensure_sign_algo_pq_safe(gnutls_sign_algorithm_t algo, GError **error)
{
#ifdef HAVE_GNUTLS_PQC
	if (algo == GNUTLS_SIGN_MLDSA44 || algo == GNUTLS_SIGN_MLDSA65 ||
	    algo == GNUTLS_SIGN_MLDSA87)
		return TRUE;
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_INVALID_DATA,
		    "%s is not PQ safe",
		    gnutls_sign_get_name(algo));
	return FALSE;
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "GnuTLS too old for PQC support");
	return FALSE;
#endif
}

/* generates a self signed certificate just like:
 *  `certtool --generate-self-signed --load-privkey priv.pem` */
GBytes *
fu_jcat_gnutls_pkcs7_create_client_certificate(gnutls_privkey_t privkey, GError **error)
{
	int rc;
	gnutls_datum_t d = {0};
	guchar sha1buf[20];
	gsize sha1bufsz = sizeof(sha1buf);
	gnutls_digest_algorithm_t digest_alg = GNUTLS_DIG_NULL;
	g_auto(gnutls_pubkey_t) pubkey = NULL;
	g_auto(gnutls_x509_crt_t) crt = NULL;
	g_autoptr(gnutls_data_t) d_payload = NULL;

	/* load the public key from the private key */
	pubkey = fu_jcat_gnutls_pkcs7_load_pubkey_from_privkey(privkey, error);
	if (pubkey == NULL)
		return NULL;

	rc = gnutls_pubkey_get_preferred_hash_algorithm(pubkey, &digest_alg, NULL);
	if (!fu_jcat_gnutls_rc_to_error(rc, error)) {
		g_prefix_error_literal(error, "failed to get preferred hash algorithm: ");
		return NULL;
	}
	g_debug("preferred_hash_algorithm=%s", gnutls_digest_get_name(digest_alg));

	/* create certificate */
	rc = gnutls_x509_crt_init(&crt);
	if (!fu_jcat_gnutls_rc_to_error(rc, error)) {
		g_prefix_error_literal(error, "failed to crt_init: ");
		return NULL;
	}

	/* set public key */
	rc = gnutls_x509_crt_set_pubkey(crt, pubkey);
	if (!fu_jcat_gnutls_rc_to_error(rc, error)) {
		g_prefix_error_literal(error, "failed to crt_set_pubkey: ");
		return NULL;
	}

	/* set positive random serial number */
	rc = gnutls_rnd(GNUTLS_RND_NONCE, sha1buf, sizeof(sha1buf));
	if (!fu_jcat_gnutls_rc_to_error(rc, error)) {
		g_prefix_error_literal(error, "failed to gnutls_rnd: ");
		return NULL;
	}
	sha1buf[0] &= 0x7f;
	rc = gnutls_x509_crt_set_serial(crt, sha1buf, sizeof(sha1buf));
	if (!fu_jcat_gnutls_rc_to_error(rc, error)) {
		g_prefix_error_literal(error, "failed to crt_set_serial: ");
		return NULL;
	}

	/* set activation */
	rc = gnutls_x509_crt_set_activation_time(crt, time(NULL));
	if (!fu_jcat_gnutls_rc_to_error(rc, error)) {
		g_prefix_error_literal(error, "failed to set activation time: ");
		return NULL;
	}

	/* set expiration */
	rc = gnutls_x509_crt_set_expiration_time(crt, (time_t)-1);
	if (!fu_jcat_gnutls_rc_to_error(rc, error)) {
		g_prefix_error_literal(error, "failed to set expiration time: ");
		return NULL;
	}

	/* set basic constraints */
	rc = gnutls_x509_crt_set_basic_constraints(crt, 0, -1);
	if (!fu_jcat_gnutls_rc_to_error(rc, error)) {
		g_prefix_error_literal(error, "failed to set basic constraints: ");
		return NULL;
	}

	/* set usage */
	rc = gnutls_x509_crt_set_key_usage(crt, GNUTLS_KEY_DIGITAL_SIGNATURE);
	if (!fu_jcat_gnutls_rc_to_error(rc, error)) {
		g_prefix_error_literal(error, "failed to set key usage: ");
		return NULL;
	}

	/* set subject key ID */
	rc = gnutls_x509_crt_get_key_id(crt, GNUTLS_KEYID_USE_SHA1, sha1buf, &sha1bufsz);
	if (!fu_jcat_gnutls_rc_to_error(rc, error)) {
		g_prefix_error_literal(error, "failed to get key id: ");
		return NULL;
	}
	rc = gnutls_x509_crt_set_subject_key_id(crt, sha1buf, sha1bufsz);
	if (!fu_jcat_gnutls_rc_to_error(rc, error)) {
		g_prefix_error_literal(error, "failed to set subject key id: ");
		return NULL;
	}

	/* set version */
	rc = gnutls_x509_crt_set_version(crt, 3);
	if (!fu_jcat_gnutls_rc_to_error(rc, error)) {
		g_prefix_error_literal(error, "failed to set certificate version: ");
		return NULL;
	}

	/* self-sign certificate */
	rc = gnutls_x509_crt_privkey_sign(crt, crt, privkey, digest_alg, 0);
	if (!fu_jcat_gnutls_rc_to_error(rc, error)) {
		g_prefix_error_literal(error, "failed to crt_privkey_sign: ");
		return NULL;
	}

	/* export to file */
	rc = gnutls_x509_crt_export2(crt, GNUTLS_X509_FMT_PEM, &d);
	if (!fu_jcat_gnutls_rc_to_error(rc, error)) {
		g_prefix_error_literal(error, "failed to crt_export2: ");
		return NULL;
	}
	d_payload = d.data;
	return g_bytes_new(d_payload, d.size);
}

static void
fu_jcat_gnutls_global_log_cb(int level, const char *msg)
{
	g_auto(GStrv) lines = g_strsplit(msg, "\n", -1);
	for (guint i = 0; lines[i] != NULL; i++) {
		if (lines[i][0] == '\0')
			continue;
		g_debug("GnuTLS: %s", lines[i]);
	}
}

void
fu_jcat_gnutls_global_init(void)
{
	gnutls_global_set_log_level(g_log_get_debug_enabled() ? 3 : 0);
	gnutls_global_set_log_function(fu_jcat_gnutls_global_log_cb);
}

gboolean
fu_jcat_gnutls_rc_to_error(int rc, GError **error)
{
	guint error_code = FWUPD_ERROR_INTERNAL;

	if (rc >= GNUTLS_E_SUCCESS)
		return TRUE;
	switch (rc) {
	case GNUTLS_E_ECC_UNSUPPORTED_CURVE:
	case GNUTLS_E_INSUFFICIENT_CREDENTIALS:
	case GNUTLS_E_INSUFFICIENT_SECURITY:
	case GNUTLS_E_NO_CERTIFICATE_FOUND:
	case GNUTLS_E_UNIMPLEMENTED_FEATURE:
	case GNUTLS_E_UNKNOWN_ALGORITHM:
	case GNUTLS_E_UNKNOWN_CIPHER_TYPE:
	case GNUTLS_E_UNKNOWN_COMPRESSION_ALGORITHM:
	case GNUTLS_E_UNKNOWN_HASH_ALGORITHM:
	case GNUTLS_E_UNKNOWN_PK_ALGORITHM:
	case GNUTLS_E_UNKNOWN_PKCS_CONTENT_TYPE:
	case GNUTLS_E_UNSUPPORTED_CERTIFICATE_TYPE:
	case GNUTLS_E_UNSUPPORTED_SIGNATURE_ALGORITHM:
	case GNUTLS_E_UNWANTED_ALGORITHM:
	case GNUTLS_E_X509_CERTIFICATE_ERROR:
	case GNUTLS_E_X509_UNSUPPORTED_ATTRIBUTE:
	case GNUTLS_E_X509_UNSUPPORTED_CRITICAL_EXTENSION:
	case GNUTLS_E_X509_UNSUPPORTED_EXTENSION:
		error_code = FWUPD_ERROR_NOT_SUPPORTED;
		break;
	case GNUTLS_E_BASE64_DECODING_ERROR:
	case GNUTLS_E_CERTIFICATE_KEY_MISMATCH:
	case GNUTLS_E_DECRYPTION_FAILED:
	case GNUTLS_E_KEY_USAGE_VIOLATION:
	case GNUTLS_E_PK_DECRYPTION_FAILED:
	case GNUTLS_E_PK_ENCRYPTION_FAILED:
	case GNUTLS_E_PK_SIGN_FAILED:
	case GNUTLS_E_PK_SIG_VERIFY_FAILED:
	case GNUTLS_E_SHORT_MEMORY_BUFFER:
	case GNUTLS_E_UNEXPECTED_PACKET_LENGTH:
	case GNUTLS_E_UNKNOWN_CIPHER_SUITE:
		error_code = FWUPD_ERROR_INVALID_DATA;
		break;
	default:
		break;
	}
	g_set_error(error, FWUPD_ERROR, error_code, "%s [%i]", gnutls_strerror(rc), rc);
	return FALSE;
}
