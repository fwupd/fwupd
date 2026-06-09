/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2025 Colin Kinloch <colin.kinloch@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#define OPENSSL_NO_DEPRECATED

/* PEM_read_bio_CMS is generated in cms.h by macros defined in pem.h */
// clang-format off
#include <openssl/pem.h>
#include <openssl/cms.h>
// clang-format on

#include <fwupdplugin.h>

#include <openssl/crypto.h>
#include <openssl/decoder.h>
#include <openssl/encoder.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pkcs7.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>
#include <openssl/x509v3.h>

/* wrap macros to define autoptr for STACK_OF(X509) */
typedef STACK_OF(X509) STACK_OF_X509;

/* nocheck:name */
void
STACK_OF_X509_free(STACK_OF_X509 *stack);

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTOPTR_CLEANUP_FUNC(BIO, BIO_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(X509, X509_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(X509_STORE, X509_STORE_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(STACK_OF_X509, STACK_OF_X509_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(CMS_ContentInfo, CMS_ContentInfo_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(OSSL_DECODER_CTX, OSSL_DECODER_CTX_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(OSSL_ENCODER_CTX, OSSL_ENCODER_CTX_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(EVP_PKEY, EVP_PKEY_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(EVP_PKEY_CTX, EVP_PKEY_CTX_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(BIGNUM, BN_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ASN1_INTEGER, ASN1_INTEGER_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ASN1_BIT_STRING, ASN1_BIT_STRING_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ASN1_OCTET_STRING, ASN1_OCTET_STRING_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(BASIC_CONSTRAINTS, BASIC_CONSTRAINTS_free)
#pragma clang diagnostic pop

X509 *
fu_jcat_libcrypto_pkcs7_load_crt_from_blob_pem(GBytes *blob, GError **error) G_GNUC_NON_NULL(1);
X509 *
fu_jcat_libcrypto_pkcs7_load_crt_from_blob_der(GBytes *blob, GError **error) G_GNUC_NON_NULL(1);
EVP_PKEY *
fu_jcat_libcrypto_pkcs7_load_privkey_from_blob_pem(GBytes *blob, GError **error) G_GNUC_NON_NULL(1);

GBytes *
fu_jcat_libcrypto_pkcs7_create_private_key(GError **error);
GBytes *
fu_jcat_libcrypto_pkcs7_create_client_certificate(EVP_PKEY *privkey, GError **error)
    G_GNUC_NON_NULL(1);

gchar *
fu_jcat_libcrypto_x509_get_issuer_name(X509 *crt, GError **error) G_GNUC_NON_NULL(1);

gchar *
fu_jcat_libcrypto_get_errors(void);
