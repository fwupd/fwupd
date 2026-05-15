/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#include <gnutls/abstract.h>
#include <gnutls/crypto.h>
#include <gnutls/pkcs7.h>

typedef guchar gnutls_data_t;

/* nocheck:name */
static void
_gnutls_datum_deinit(gnutls_datum_t *d)
{
	gnutls_free(d->data);
	gnutls_free(d);
}

/* nocheck:name */
static void
_gnutls_x509_trust_list_deinit(gnutls_x509_trust_list_t tl)
{
	gnutls_x509_trust_list_deinit(tl, 0);
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTO_CLEANUP_FREE_FUNC(gnutls_pkcs7_t, gnutls_pkcs7_deinit, NULL)
G_DEFINE_AUTO_CLEANUP_FREE_FUNC(gnutls_privkey_t, gnutls_privkey_deinit, NULL)
G_DEFINE_AUTO_CLEANUP_FREE_FUNC(gnutls_pubkey_t, gnutls_pubkey_deinit, NULL)
G_DEFINE_AUTO_CLEANUP_FREE_FUNC(gnutls_x509_crt_t, gnutls_x509_crt_deinit, NULL)
G_DEFINE_AUTO_CLEANUP_FREE_FUNC(gnutls_x509_dn_t, gnutls_x509_dn_deinit, NULL)
G_DEFINE_AUTO_CLEANUP_FREE_FUNC(gnutls_x509_privkey_t, gnutls_x509_privkey_deinit, NULL)
G_DEFINE_AUTO_CLEANUP_FREE_FUNC(gnutls_x509_spki_t, gnutls_x509_spki_deinit, NULL)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(gnutls_data_t, gnutls_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(gnutls_pkcs7_signature_info_st, gnutls_pkcs7_signature_info_deinit)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(gnutls_datum_t, _gnutls_datum_deinit)
G_DEFINE_AUTO_CLEANUP_FREE_FUNC(gnutls_x509_trust_list_t, _gnutls_x509_trust_list_deinit, NULL)
G_DEFINE_AUTO_CLEANUP_FREE_FUNC(gnutls_x509_trust_list_iter_t,
				gnutls_x509_trust_list_iter_deinit,
				NULL)
#pragma clang diagnostic pop

void
fu_jcat_gnutls_global_init(void);
gboolean
fu_jcat_gnutls_datum_from_bytes(gnutls_datum_t *d, GBytes *blob, GError **error);
gboolean
fu_jcat_gnutls_rc_to_error(int rc, GError **error);
gchar *
fu_jcat_gnutls_pkcs7_datum_to_dn_str(const gnutls_datum_t *raw) G_GNUC_NON_NULL(1);
gnutls_x509_crt_t
fu_jcat_gnutls_pkcs7_load_crt_from_blob(GBytes *blob, gnutls_x509_crt_fmt_t format, GError **error)
    G_GNUC_NON_NULL(1);
gnutls_privkey_t
fu_jcat_gnutls_pkcs7_load_privkey_from_blob(GBytes *blob, GError **error) G_GNUC_NON_NULL(1);
gnutls_pubkey_t
fu_jcat_gnutls_pkcs7_load_pubkey_from_privkey(gnutls_privkey_t privkey, GError **error)
    G_GNUC_NON_NULL(1);
gboolean
fu_jcat_gnutls_ensure_trust_list_valid(gnutls_x509_trust_list_t tl, GError **error)
    G_GNUC_NON_NULL(1);

GBytes *
fu_jcat_gnutls_pkcs7_create_private_key(gnutls_pk_algorithm_t algo, GError **error);
gboolean
fu_jcat_gnutls_pkcs7_ensure_sign_algo_pq_safe(gnutls_sign_algorithm_t algo, GError **error);

GBytes *
fu_jcat_gnutls_pkcs7_create_client_certificate(gnutls_privkey_t privkey, GError **error)
    G_GNUC_NON_NULL(1);
