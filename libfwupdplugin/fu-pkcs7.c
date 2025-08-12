/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#ifdef HAVE_GNUTLS
#include <gnutls/abstract.h>
#include <gnutls/crypto.h>
#include <gnutls/pkcs7.h>
#endif

#include "fu-input-stream.h"
#include "fu-pkcs7.h"
#include "fu-x509-certificate.h"

#ifdef HAVE_GNUTLS
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTO_CLEANUP_FREE_FUNC(gnutls_pkcs7_t, gnutls_pkcs7_deinit, NULL)
#pragma clang diagnostic pop
#endif

/**
 * FuPkcs7:
 *
 * A PKCS#7 object, typically containing signed X.509 certificates.
 *
 * See also: [class@FuFirmware]
 */

struct _FuPkcs7 {
	FuFirmware parent_instance;
};

G_DEFINE_TYPE(FuPkcs7, fu_pkcs7, FU_TYPE_FIRMWARE)

#ifdef HAVE_GNUTLS
static gboolean
fu_pkcs7_parse_x509_certificate(FuPkcs7 *self, gnutls_datum_t *data, GError **error)
{
	g_autoptr(FuX509Certificate) crt = fu_x509_certificate_new();
	g_autoptr(GBytes) blob = NULL;

	/* parse as a X.509 certificate */
	blob = g_bytes_new(data->data, data->size);
	if (!fu_firmware_parse_bytes(FU_FIRMWARE(crt),
				     blob,
				     0x0,
				     FU_FIRMWARE_PARSE_FLAG_NONE,
				     error))
		return FALSE;
	fu_firmware_add_image(FU_FIRMWARE(self), FU_FIRMWARE(crt));

	/* success */
	return TRUE;
}
#endif

static gboolean
fu_pkcs7_parse(FuFirmware *firmware,
	       GInputStream *stream,
	       FuFirmwareParseFlags flags,
	       GError **error)
{
#ifdef HAVE_GNUTLS
	FuPkcs7 *self = FU_PKCS7(firmware);
	gnutls_datum_t datum = {0};
	int rc;
	g_auto(gnutls_pkcs7_t) pkcs7 = NULL;
	g_autoptr(GByteArray) buf = NULL;

	/* load PKCS#7 cert */
	buf = fu_input_stream_read_byte_array(stream, 0x0, G_MAXSIZE, NULL, error);
	if (buf == NULL)
		return FALSE;
	rc = gnutls_pkcs7_init(&pkcs7);
	if (rc != GNUTLS_E_SUCCESS) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "failed to init pkcs7: %s [%i]",
			    gnutls_strerror(rc),
			    rc);
		return FALSE;
	}
	datum.data = buf->data;
	datum.size = buf->len;
	rc = gnutls_pkcs7_import(pkcs7, &datum, GNUTLS_X509_FMT_DER);
	if (rc != GNUTLS_E_SUCCESS) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "failed to import the PKCS7 signature: %s [%i]",
			    gnutls_strerror(rc),
			    rc);
		return FALSE;
	}

	/* parse each X.509 certificate */
	for (int i = 0; i < gnutls_pkcs7_get_crt_count(pkcs7); i++) {
		gnutls_datum_t out;
		rc = gnutls_pkcs7_get_crt_raw2(pkcs7, i, &out);
		if (rc < 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "failed to get raw crt: %s [%i]",
				    gnutls_strerror(rc),
				    rc);
			return FALSE;
		}
		if (!fu_pkcs7_parse_x509_certificate(self, &out, error)) {
			gnutls_free(out.data);
			return FALSE;
		}
		gnutls_free(out.data);
	}

	/* success */
	return TRUE;
#else
	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no GnuTLS support");
	return FALSE;
#endif
}

static void
fu_pkcs7_init(FuPkcs7 *self)
{
}

static void
fu_pkcs7_class_init(FuPkcs7Class *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_pkcs7_parse;
}

/**
 * fu_pkcs7_new:
 *
 * Creates a new #FuPkcs7.
 *
 * Returns: (transfer full): object
 *
 * Since: 2.0.9
 **/
FuPkcs7 *
fu_pkcs7_new(void)
{
	return g_object_new(FU_TYPE_PKCS7, NULL);
}
