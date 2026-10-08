/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

static void
fu_x509_certificate_func(void)
{
	gboolean ret;
	g_autofree gchar *str = NULL;
	g_autoptr(FuX509Certificate) cert = fu_x509_certificate_new();
	g_autoptr(FuX509Certificate) cert2 = fu_x509_certificate_new();
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GDateTime) activation_time = NULL;
	g_autoptr(GError) error = NULL;

	/* trivial setters and getters */
	fu_x509_certificate_set_issuer(cert, "O=fwupd,CN=issuer");
	fu_x509_certificate_set_subject(cert, "O=fwupd,CN=subject");
	g_assert_cmpstr(fu_x509_certificate_get_issuer(cert), ==, "O=fwupd,CN=issuer");
	g_assert_cmpstr(fu_x509_certificate_get_subject(cert), ==, "O=fwupd,CN=subject");

	/* the export vfunc dumps the issuer and subject */
	str = fu_firmware_to_string(FU_FIRMWARE(cert));
	g_assert_nonnull(str);
	g_assert_nonnull(g_strstr_len(str, -1, "O=fwupd,CN=subject"));

	/* write out a self-signed certificate, then parse it back in */
	fu_x509_certificate_set_subject(cert, "O=fwupd");
	fu_x509_certificate_set_activation_time(cert, 1600000000);
	blob = fu_firmware_write(FU_FIRMWARE(cert), &error);
	if (blob == NULL && g_error_matches(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED)) {
		g_test_skip("no GnuTLS support");
		return;
	}
	g_assert_no_error(error);
	g_assert_nonnull(blob);

	ret = fu_firmware_parse_bytes(FU_FIRMWARE(cert2),
				      blob,
				      0x0,
				      FU_FIRMWARE_PARSE_FLAG_NONE,
				      &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* self-signed, so the issuer matches the subject */
	g_assert_nonnull(g_strstr_len(fu_x509_certificate_get_subject(cert2), -1, "fwupd"));
	g_assert_nonnull(g_strstr_len(fu_x509_certificate_get_issuer(cert2), -1, "fwupd"));

	/* the activation time survives the round trip */
	activation_time = fu_x509_certificate_get_activation_time(cert2);
	g_assert_nonnull(activation_time);
	g_assert_cmpint(g_date_time_to_unix(activation_time), ==, 1600000000);

	/* the key ID is used as the firmware ID */
	g_assert_nonnull(fu_firmware_get_id(FU_FIRMWARE(cert2)));
}

static void
fu_x509_certificate_parse_invalid_func(void)
{
	gboolean ret;
	g_autoptr(FuX509Certificate) cert = fu_x509_certificate_new();
	g_autoptr(GBytes) blob = g_bytes_new_static("notacert", 8);
	g_autoptr(GError) error = NULL;

	ret = fu_firmware_parse_bytes(FU_FIRMWARE(cert),
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
	g_test_add_func("/fwupd/x509-certificate", fu_x509_certificate_func);
	g_test_add_func("/fwupd/x509-certificate/parse-invalid",
			fu_x509_certificate_parse_invalid_func);
	return g_test_run();
}
