/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

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

G_DEFINE_AUTO_CLEANUP_FREE_FUNC(gnutls_pkcs7_t, gnutls_pkcs7_deinit, NULL)

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

	/* load file and add to the trust list */
	if (!g_file_get_contents (filename, &pem_data, &sz, error)) {
		g_prefix_error (error, "failed to load %s: ", filename);
		return FALSE;
	}
	datum.data = (guint8 *) pem_data;
	datum.size = sz;
	rc = gnutls_x509_trust_list_add_trust_mem (self->tl, &datum,
						   NULL, /* crls */
						   format,
						   0, /* tl_flags */
						   0); /* tl_vflags */
	if (rc < 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_SIGNATURE_INVALID,
			     "failed to add to trust list: %s [%i]",
			     gnutls_strerror (rc), rc);
		return FALSE;
	}
	g_debug ("loaded %i CAs", rc);
	return TRUE;
}

static gboolean
fu_keyring_pkcs7_setup (FuKeyring *keyring, const gchar *public_key_dir, GError **error)
{
	FuKeyringPkcs7 *self = FU_KEYRING_PKCS7 (keyring);
	const gchar *fn_tmp;
	int rc;
	g_autoptr(GDir) dir = NULL;

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

	/* search all the public key files */
	dir = g_dir_open (public_key_dir, 0, error);
	if (dir == NULL)
		return FALSE;
	while ((fn_tmp = g_dir_read_name (dir)) != NULL) {
		g_autofree gchar *path_tmp = NULL;
		path_tmp = g_build_filename (public_key_dir, fn_tmp, NULL);
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
fu_keyring_pkcs7_verify_data (FuKeyring *keyring,
			     GBytes *blob,
			     GBytes *blob_signature,
			     GError **error)
{
	FuKeyringPkcs7 *self = FU_KEYRING_PKCS7 (keyring);
	gnutls_datum_t datum;
	int count;
	int rc;
	g_auto(gnutls_pkcs7_t) pkcs7 = NULL;

	/* startup */
	rc = gnutls_pkcs7_init (&pkcs7);
	if (rc != GNUTLS_E_SUCCESS) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_SIGNATURE_INVALID,
			     "failed to init pkcs7: %s [%i]",
			     gnutls_strerror (rc), rc);
		return FALSE;
	}

	/* import the signature */
	datum.data = g_bytes_get_data (blob_signature, NULL);
	datum.size = g_bytes_get_size (blob_signature);
	rc = gnutls_pkcs7_import (pkcs7, &datum, GNUTLS_X509_FMT_PEM);
	if (rc != GNUTLS_E_SUCCESS) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_SIGNATURE_INVALID,
			     "failed to import the PKCS7 signature: %s [%i]",
			     gnutls_strerror (rc), rc);
		return FALSE;
	}

	/* verify the blob */
	datum.data = g_bytes_get_data (blob, NULL);
	datum.size = g_bytes_get_size (blob);
	count = gnutls_pkcs7_get_signature_count (pkcs7);
	g_debug ("got %i PKCS7 signatures", count);
	if (count == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_SIGNATURE_INVALID,
				     "no PKCS7 signatures found");
		return FALSE;
	}
	for (gint i = 0; i < count; i++) {
		rc = gnutls_pkcs7_verify (pkcs7, self->tl,
					  NULL, /* vdata */
					  0,    /* vdata_size */
					  i,    /* index */
					  &datum, /* data */
					  0);   /* flags */
		if (rc < 0) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_SIGNATURE_INVALID,
				     "failed to verify data: %s [%i]",
				     gnutls_strerror (rc), rc);
			return FALSE;
		}
	}

	/* success */
	return TRUE;
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
