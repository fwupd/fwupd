/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

/* a DER-encoded PKCS#7 "certs-only" bundle containing a single self-signed
 * certificate with the subject "O=fwupd,CN=fwupd self test" */
static const gchar *pkcs7_base64 =
    "MIIB2AYJKoZIhvcNAQcCoIIByTCCAcUCAQExADALBgkqhkiG9w0BBwGgggGtMIIBqTCCAU+gAwIB"
    "AgIUOQhiAg/jDGylZNsIa/pOrbR5bGIwCgYIKoZIzj0EAwIwKjEOMAwGA1UECgwFZnd1cGQxGDAW"
    "BgNVBAMMD2Z3dXBkIHNlbGYgdGVzdDAeFw0yNjEwMDgxMDI2NTVaFw0zNjEwMDUxMDI2NTVaMCox"
    "DjAMBgNVBAoMBWZ3dXBkMRgwFgYDVQQDDA9md3VwZCBzZWxmIHRlc3QwWTATBgcqhkjOPQIBBggq"
    "hkjOPQMBBwNCAAQmSifCEFzH0NiXwuIs6moG+rcW4Sh9O9LRRe6hjSAhcf2KVVRUBOzGVmndAvHjl"
    "LmRtddxhVTAPIuc/9sNgu9lo1MwUTAdBgNVHQ4EFgQUh0XTPgd66Q3/MVI2nbVfc3nEx2gwHwYDVR"
    "0jBBgwFoAUh0XTPgd66Q3/MVI2nbVfc3nEx2gwDwYDVR0TAQH/BAUwAwEB/zAKBggqhkjOPQQDAgN"
    "IADBFAiAoC7DeCwq9g9L11SjhUFKYQNrOzS4ftyT2OYe2RsQ+/AIhALlYqbFpKvok7RWpvVfudhhd"
    "CmrTof/LLV92c+518YJ9MQA=";

static void
fu_pkcs7_func(void)
{
	gboolean ret;
	gsize bufsz = 0;
	g_autofree guint8 *buf = NULL;
	g_autoptr(FuFirmware) img = NULL;
	g_autoptr(FuPkcs7) pkcs7 = fu_pkcs7_new();
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;

	buf = g_base64_decode(pkcs7_base64, &bufsz);
	g_assert_nonnull(buf);
	blob = g_bytes_new_take(g_steal_pointer(&buf), bufsz);

	ret = fu_firmware_parse_bytes(FU_FIRMWARE(pkcs7),
				      blob,
				      0x0,
				      FU_FIRMWARE_PARSE_FLAG_NONE,
				      &error);
	if (!ret && g_error_matches(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED)) {
		g_test_skip("no GnuTLS support");
		return;
	}
	g_assert_no_error(error);
	g_assert_true(ret);

	/* the bundle has a single embedded X.509 certificate */
	img = fu_firmware_get_image_by_idx(FU_FIRMWARE(pkcs7), 0x0, &error);
	g_assert_no_error(error);
	g_assert_nonnull(img);
	g_assert_true(FU_IS_X509_CERTIFICATE(img));
	g_assert_nonnull(
	    g_strstr_len(fu_x509_certificate_get_subject(FU_X509_CERTIFICATE(img)), -1, "fwupd"));
}

static void
fu_pkcs7_parse_invalid_func(void)
{
	gboolean ret;
	g_autoptr(FuPkcs7) pkcs7 = fu_pkcs7_new();
	g_autoptr(GBytes) blob = g_bytes_new_static("notapkcs7", 9);
	g_autoptr(GError) error = NULL;

	ret = fu_firmware_parse_bytes(FU_FIRMWARE(pkcs7),
				      blob,
				      0x0,
				      FU_FIRMWARE_PARSE_FLAG_NONE,
				      &error);
	if (!ret && g_error_matches(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED)) {
		g_test_skip("no GnuTLS support");
		return;
	}
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_false(ret);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/pkcs7", fu_pkcs7_func);
	g_test_add_func("/fwupd/pkcs7/parse-invalid", fu_pkcs7_parse_invalid_func);
	return g_test_run();
}
