/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#ifdef HAVE_GNUTLS
#include <gnutls/abstract.h>
#include <gnutls/crypto.h>
#endif

#include "fu-common.h"
#include "fu-input-stream.h"
#include "fu-string.h"
#include "fu-x509-certificate.h"

#ifdef HAVE_GNUTLS
static void
fu_x509_certificate_gnutls_datum_deinit(gnutls_datum_t *d)
{
	gnutls_free(d->data);
	gnutls_free(d);
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTOPTR_CLEANUP_FUNC(gnutls_datum_t, fu_x509_certificate_gnutls_datum_deinit)
G_DEFINE_AUTO_CLEANUP_FREE_FUNC(gnutls_x509_crt_t, gnutls_x509_crt_deinit, NULL)
#pragma clang diagnostic pop
#endif

/**
 * FuX509Certificate:
 *
 * An X.509 certificate.
 *
 * See also: [class@FuFirmware]
 */

struct _FuX509Certificate {
	FuFirmware parent_instance;
	gchar *issuer;
	gchar *subject;
};

G_DEFINE_TYPE(FuX509Certificate, fu_x509_certificate, FU_TYPE_FIRMWARE)

static void
fu_x509_certificate_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuX509Certificate *self = FU_X509_CERTIFICATE(firmware);
	fu_xmlb_builder_insert_kv(bn, "issuer", self->issuer);
	fu_xmlb_builder_insert_kv(bn, "subject", self->subject);
}

/**
 * fu_x509_certificate_get_issuer:
 * @self: A #FuX509Certificate
 *
 * Returns the certificate issuer.
 *
 * Returns: string, or %NULL for unset
 *
 * Since: 2.0.9
 **/
const gchar *
fu_x509_certificate_get_issuer(FuX509Certificate *self)
{
	g_return_val_if_fail(FU_IS_X509_CERTIFICATE(self), NULL);
	return self->issuer;
}

static void
fu_x509_certificate_set_issuer(FuX509Certificate *self, const gchar *issuer)
{
	g_return_if_fail(FU_IS_X509_CERTIFICATE(self));
	if (g_strcmp0(issuer, self->issuer) == 0)
		return;
	g_free(self->issuer);
	self->issuer = g_strdup(issuer);
}

static void
fu_x509_certificate_set_subject(FuX509Certificate *self, const gchar *subject)
{
	g_return_if_fail(FU_IS_X509_CERTIFICATE(self));
	if (g_strcmp0(subject, self->subject) == 0)
		return;
	g_free(self->subject);
	self->subject = g_strdup(subject);
}

/**
 * fu_x509_certificate_get_subject:
 * @self: A #FuX509Certificate
 *
 * Returns the certificate subject.
 *
 * Returns: string, or %NULL for unset
 *
 * Since: 2.0.9
 **/
const gchar *
fu_x509_certificate_get_subject(FuX509Certificate *self)
{
	g_return_val_if_fail(FU_IS_X509_CERTIFICATE(self), NULL);
	return self->subject;
}

static gboolean
fu_x509_certificate_parse(FuFirmware *firmware,
			  GInputStream *stream,
			  FuFirmwareParseFlags flags,
			  GError **error)
{
#ifdef HAVE_GNUTLS
	FuX509Certificate *self = FU_X509_CERTIFICATE(firmware);
	gchar buf[1024] = {'\0'};
	guchar key_id[32] = {'\0'};
	gsize key_idsz = sizeof(key_id);
	gnutls_datum_t d = {0};
	gnutls_x509_dn_t dn = {0x0};
	gsize bufsz = sizeof(buf);
	int rc;
	g_auto(gnutls_x509_crt_t) crt = NULL;
	g_autoptr(gnutls_datum_t) subject = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GString) key_idstr = g_string_new(NULL);

	/* parse certificate */
	blob = fu_input_stream_read_bytes(stream, 0x0, G_MAXSIZE, NULL, error);
	if (blob == NULL)
		return FALSE;
	d.size = g_bytes_get_size(blob);
	d.data = (unsigned char *)g_bytes_get_data(blob, NULL);

	rc = gnutls_x509_crt_init(&crt);
	if (rc < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "crt_init: %s [%i]",
			    gnutls_strerror(rc),
			    rc);
		return FALSE;
	}
	rc = gnutls_x509_crt_import(crt, &d, GNUTLS_X509_FMT_DER);
	if (rc < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "crt_import: %s [%i]",
			    gnutls_strerror(rc),
			    rc);
		return FALSE;
	}

	/* issuer */
	if (gnutls_x509_crt_get_issuer_dn(crt, buf, &bufsz) == GNUTLS_E_SUCCESS) {
		g_autofree gchar *str = fu_strsafe((const gchar *)buf, bufsz);
		fu_x509_certificate_set_issuer(self, str);
	}

	/* subject */
	subject = (gnutls_datum_t *)gnutls_malloc(sizeof(gnutls_datum_t));
	if (gnutls_x509_crt_get_subject(crt, &dn) == GNUTLS_E_SUCCESS) {
		g_autofree gchar *str = NULL;
		gnutls_x509_dn_get_str(dn, subject);
		str = fu_strsafe((const gchar *)subject->data, subject->size);
		fu_x509_certificate_set_subject(self, str);
	}

	/* key ID */
	rc = gnutls_x509_crt_get_key_id(crt, 0, key_id, &key_idsz);
	if (rc < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "failed to get key ID: %s [%i]",
			    gnutls_strerror(rc),
			    rc);
		return FALSE;
	}
	for (guint i = 0; i < key_idsz; i++)
		g_string_append_printf(key_idstr, "%02x", key_id[i]);
	fu_firmware_set_id(firmware, key_idstr->str);

	/* success */
	return TRUE;
#else
	g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no GnuTLS support");
	return FALSE;
#endif
}

static void
fu_x509_certificate_init(FuX509Certificate *self)
{
}

static void
fu_x509_certificate_finalize(GObject *obj)
{
	FuX509Certificate *self = FU_X509_CERTIFICATE(obj);
	g_free(self->issuer);
	g_free(self->subject);
	G_OBJECT_CLASS(fu_x509_certificate_parent_class)->finalize(obj);
}

static void
fu_x509_certificate_class_init(FuX509CertificateClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	object_class->finalize = fu_x509_certificate_finalize;
	firmware_class->export = fu_x509_certificate_export;
	firmware_class->parse = fu_x509_certificate_parse;
}

/**
 * fu_x509_certificate_new:
 *
 * Creates a new #FuX509Certificate.
 *
 * Returns: (transfer full): object
 *
 * Since: 2.0.9
 **/
FuX509Certificate *
fu_x509_certificate_new(void)
{
	return g_object_new(FU_TYPE_X509_CERTIFICATE, NULL);
}
