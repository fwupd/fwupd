/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN				"FuKeyring"

#include "config.h"

#include <gnutls/pkcs7.h>

#include "fu-keyring-pkcs7.h"

#include "fwupd-error.h"

struct _FuKeyringPkcs7
{
	FuKeyring			 parent_instance;
	gnutls_x509_trust_list_t	 tl;
};

G_DEFINE_TYPE (FuKeyringPkcs7, fu_keyring_pkcs7, FU_TYPE_KEYRING)

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTO_CLEANUP_FREE_FUNC(gnutls_pkcs7_t, gnutls_pkcs7_deinit, NULL)
G_DEFINE_AUTO_CLEANUP_FREE_FUNC(gnutls_x509_crt_t, gnutls_x509_crt_deinit, NULL)
G_DEFINE_AUTO_CLEANUP_FREE_FUNC(gnutls_x509_dn_t, gnutls_x509_dn_deinit, NULL)
#pragma clang diagnostic pop

static gboolean
fu_keyring_pkcs7_add_public_key (FuKeyringPkcs7 *self,
				 const gchar *filename,
				 gnutls_x509_crt_fmt_t format,
				 GError **error)
{
	gnutls_datum_t datum;
	gsize sz;
	int rc;
	g_autofree gchar *pem_data = NULL;
	g_auto(gnutls_x509_crt_t) cert = NULL;

	/* load file and add to the trust list */
	if (!g_file_get_contents (filename, &pem_data, &sz, error)) {
		g_prefix_error (error, "failed to load %s: ", filename);
		return FALSE;
	}
	datum.data = (guint8 *) pem_data;
	datum.size = sz;
	g_debug ("trying to load CA from %s", filename);
	rc = gnutls_x509_crt_init (&cert);
	if (rc < 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_SIGNATURE_INVALID,
			     "failed to initialize certificate: %s [%i]",
			     gnutls_strerror (rc), rc);
		return FALSE;
	}
	rc = gnutls_x509_crt_import (cert, &datum, format);
	if (rc < 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_SIGNATURE_INVALID,
			     "failed to import certificate: %s [%i]",
			     gnutls_strerror (rc), rc);
		return FALSE;
	}
	if (gnutls_x509_crt_check_key_purpose (cert, GNUTLS_KP_ANY, 0) != 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_SIGNATURE_INVALID,
			     "certificate %s not suitable for use",
			     filename);
		return FALSE;
	}
	rc = gnutls_x509_trust_list_add_cas (self->tl, &cert, 1, 0);
	if (rc < 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_SIGNATURE_INVALID,
			     "failed to add to trust list: %s [%i]",
			     gnutls_strerror (rc), rc);
		return FALSE;
	}
	g_debug ("loaded %i CAs", rc);

	/* confusingly the trust list does not copy the certificate */
	cert = NULL;
	return TRUE;
}

static gboolean
fu_keyring_pkcs7_add_public_keys (FuKeyring *keyring,
				  const gchar *path,
				  GError **error)
{
	FuKeyringPkcs7 *self = FU_KEYRING_PKCS7 (keyring);
	const gchar *fn_tmp;
	g_autoptr(GDir) dir = NULL;

	/* search all the public key files */
	dir = g_dir_open (path, 0, error);
	if (dir == NULL)
		return FALSE;
	while ((fn_tmp = g_dir_read_name (dir)) != NULL) {
		g_autofree gchar *path_tmp = NULL;
		path_tmp = g_build_filename (path, fn_tmp, NULL);
		if (g_str_has_suffix (fn_tmp, ".pem")) {
			if (!fu_keyring_pkcs7_add_public_key (self, path_tmp,
							      GNUTLS_X509_FMT_PEM,
							      error))
				return FALSE;
		}
		if (g_str_has_suffix (fn_tmp, ".cer") ||
		    g_str_has_suffix (fn_tmp, ".crt") ||
		    g_str_has_suffix (fn_tmp, ".der")) {
			if (!fu_keyring_pkcs7_add_public_key (self, path_tmp,
							      GNUTLS_X509_FMT_DER,
							      error))
				return FALSE;
		}
	}
	return TRUE;
}

static gboolean
fu_keyring_pkcs7_setup (FuKeyring *keyring, GError **error)
{
	FuKeyringPkcs7 *self = FU_KEYRING_PKCS7 (keyring);
	int rc;

	if (self->tl != NULL)
		return TRUE;

	/* create trust list, a bit like a keyring */
	rc = gnutls_x509_trust_list_init (&self->tl, 0);
	if (rc != GNUTLS_E_SUCCESS) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_SIGNATURE_INVALID,
			     "failed to create trust list: %s [%i]",
			     gnutls_strerror (rc), rc);
		return FALSE;
	}
	return TRUE;
}

static void
_gnutls_datum_deinit (gnutls_datum_t *d)
{
	gnutls_free (d->data);
	gnutls_free (d);
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTOPTR_CLEANUP_FUNC(gnutls_datum_t, _gnutls_datum_deinit)
#pragma clang diagnostic pop

static gchar *
fu_keyring_pkcs7_datum_to_dn_str (const gnutls_datum_t *raw)
{
	g_auto(gnutls_x509_dn_t) dn = NULL;
	g_autoptr(gnutls_datum_t) str = NULL;
	int rc;
	rc = gnutls_x509_dn_init (&dn);
	if (rc < 0)
		return NULL;
	rc = gnutls_x509_dn_import (dn, raw);
	if (rc < 0)
		return NULL;
	str = (gnutls_datum_t *) gnutls_malloc (sizeof (gnutls_datum_t));
	rc = gnutls_x509_dn_get_str2 (dn, str, 0);
	if (rc < 0)
		return NULL;
	return g_strndup ((const gchar *) str->data, str->size);
}

static FuKeyringResult *
fu_keyring_pkcs7_verify_data (FuKeyring *keyring,
			     GBytes *blob,
			     GBytes *blob_signature,
			     FuKeyringVerifyFlags flags,
			     GError **error)
{
	FuKeyringPkcs7 *self = FU_KEYRING_PKCS7 (keyring);
	gnutls_datum_t datum;
	gint64 timestamp_newest = 0;
	int count;
	int rc;
	g_auto(gnutls_pkcs7_t) pkcs7 = NULL;
	g_autoptr(GString) authority_newest = g_string_new (NULL);

	/* startup */
	rc = gnutls_pkcs7_init (&pkcs7);
	if (rc != GNUTLS_E_SUCCESS) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_SIGNATURE_INVALID,
			     "failed to init pkcs7: %s [%i]",
			     gnutls_strerror (rc), rc);
		return NULL;
	}

	/* import the signature */
	datum.data = (guchar *) g_bytes_get_data (blob_signature, NULL);
	datum.size = g_bytes_get_size (blob_signature);
	rc = gnutls_pkcs7_import (pkcs7, &datum, GNUTLS_X509_FMT_PEM);
	if (rc != GNUTLS_E_SUCCESS) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_SIGNATURE_INVALID,
			     "failed to import the PKCS7 signature: %s [%i]",
			     gnutls_strerror (rc), rc);
		return NULL;
	}

	/* verify the blob */
	datum.data = (guchar *) g_bytes_get_data (blob, NULL);
	datum.size = g_bytes_get_size (blob);
	count = gnutls_pkcs7_get_signature_count (pkcs7);
	g_debug ("got %i PKCS7 signatures", count);
	if (count == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_SIGNATURE_INVALID,
				     "no PKCS7 signatures found");
		return NULL;
	}
	for (gint i = 0; i < count; i++) {
		gnutls_pkcs7_signature_info_st info;
		gint64 signing_time = 0;
		gnutls_certificate_verify_flags verify_flags = 0;

		/* use with care */
		if (flags & FU_KEYRING_VERIFY_FLAG_DISABLE_TIME_CHECKS) {
			g_debug ("WARNING: disabling time checks");
			verify_flags |= GNUTLS_VERIFY_DISABLE_TIME_CHECKS;
			verify_flags |= GNUTLS_VERIFY_DISABLE_TRUSTED_TIME_CHECKS;
		}

		/* verify the data against the detached signature */
		rc = gnutls_pkcs7_verify (pkcs7, self->tl,
					  NULL, /* vdata */
					  0,    /* vdata_size */
					  i,    /* index */
					  &datum, /* data */
					  verify_flags);   /* flags */
		if (rc < 0) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_SIGNATURE_INVALID,
				     "failed to verify data: %s [%i]",
				     gnutls_strerror (rc), rc);
			return NULL;
		}

		/* save details about the key for the result */
		rc = gnutls_pkcs7_get_signature_info (pkcs7, i, &info);
		if (rc < 0) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_SIGNATURE_INVALID,
				     "failed to get signature info: %s [%i]",
				     gnutls_strerror (rc), rc);
			return NULL;
		}
		signing_time = info.signing_time > 0 ? (gint64) info.signing_time : 1;
		if (signing_time > timestamp_newest) {
			g_autofree gchar *dn = NULL;
			timestamp_newest = signing_time;
			dn = fu_keyring_pkcs7_datum_to_dn_str (&info.issuer_dn);
			g_string_assign (authority_newest, dn);
		}
		gnutls_pkcs7_signature_info_deinit (&info);
	}

	/* success */
	return FU_KEYRING_RESULT (g_object_new (FU_TYPE_KEYRING_RESULT,
						"timestamp", timestamp_newest,
						"authority", authority_newest->str,
						NULL));
}

static void
fu_keyring_pkcs7_finalize (GObject *object)
{
	FuKeyringPkcs7 *self = FU_KEYRING_PKCS7 (object);
	gnutls_x509_trust_list_deinit (self->tl, 1);
	G_OBJECT_CLASS (fu_keyring_pkcs7_parent_class)->finalize (object);
}

static void
fu_keyring_pkcs7_class_init (FuKeyringPkcs7Class *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	FuKeyringClass *klass_app = FU_KEYRING_CLASS (klass);
	klass_app->setup = fu_keyring_pkcs7_setup;
	klass_app->add_public_keys = fu_keyring_pkcs7_add_public_keys;
	klass_app->verify_data = fu_keyring_pkcs7_verify_data;
	object_class->finalize = fu_keyring_pkcs7_finalize;
}

static void
fu_keyring_pkcs7_init (FuKeyringPkcs7 *self)
{
	FuKeyring *keyring = FU_KEYRING (self);
	g_autofree gchar *name = NULL;
	name = g_strdup_printf ("gnutls-v%s", gnutls_check_version (NULL));
	fu_keyring_set_name (keyring, name);
}

FuKeyring *
fu_keyring_pkcs7_new (void)
{
	return FU_KEYRING (g_object_new (FU_TYPE_KEYRING_PKCS7, NULL));
}
