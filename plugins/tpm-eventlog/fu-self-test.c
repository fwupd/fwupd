/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupd.h>

#include "fu-tpm-eventlog-common.h"
#include "fu-tpm-eventlog-device.h"

static void
fu_test_tpm_eventlog_parse_v1_func (void)
{
	const gchar *tmp;
	gboolean ret;
	gsize bufsz = 0;
	g_autofree gchar *fn = NULL;
	g_autofree guint8 *buf = NULL;
	g_autofree gchar *str = NULL;
	g_autoptr(FuTpmEventlogDevice) dev = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) pcr0s = NULL;

	fn = g_build_filename (TESTDATADIR, "binary_bios_measurements-v1", NULL);
	ret = g_file_get_contents (fn, (gchar **) &buf, &bufsz, &error);
	g_assert_no_error (error);
	g_assert_true (ret);

	dev = fu_tpm_eventlog_device_new (buf, bufsz, &error);
	g_assert_no_error (error);
	g_assert_nonnull (dev);
	str = fu_device_to_string (FU_DEVICE (dev));
	g_print ("%s\n", str);
	g_assert_nonnull (g_strstr_len (str, -1, "231f248f12ef9f38549f1bda7a859b781b5caab0"));
	g_assert_nonnull (g_strstr_len (str, -1, "9069ca78e7450a285173431b3e52c5c25299e473"));

	pcr0s = fu_tpm_eventlog_device_get_checksums (dev, 0, &error);
	g_assert_no_error (error);
	g_assert_nonnull (pcr0s);
	g_assert_cmpint (pcr0s->len, ==, 1);
	tmp = g_ptr_array_index (pcr0s, 0);
	g_assert_cmpstr (tmp, ==, "543ae96e57b6fc4003531cd0dab1d9ba7f8166e0");
}

static void
fu_test_tpm_eventlog_parse_v2_func (void)
{
	const gchar *tmp;
	gboolean ret;
	gsize bufsz = 0;
	g_autofree gchar *fn = NULL;
	g_autofree guint8 *buf = NULL;
	g_autofree gchar *str = NULL;
	g_autoptr(FuTpmEventlogDevice) dev = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) pcr0s = NULL;

	fn = g_build_filename (TESTDATADIR, "binary_bios_measurements-v2", NULL);
	ret = g_file_get_contents (fn, (gchar **) &buf, &bufsz, &error);
	g_assert_no_error (error);
	g_assert_true (ret);

	dev = fu_tpm_eventlog_device_new (buf, bufsz, &error);
	g_assert_no_error (error);
	g_assert_nonnull (dev);
	str = fu_device_to_string (FU_DEVICE (dev));
	g_print ("%s\n", str);
	g_assert_nonnull (g_strstr_len (str, -1, "19ce8e1347a709d2b485d519695e3ce10b939485"));
	g_assert_nonnull (g_strstr_len (str, -1, "9069ca78e7450a285173431b3e52c5c25299e473"));
	g_assert_nonnull (g_strstr_len (str, -1, "Boot Guard Measured"));

	pcr0s = fu_tpm_eventlog_device_get_checksums (dev, 0, &error);
	g_assert_no_error (error);
	g_assert_nonnull (pcr0s);
	g_assert_cmpint (pcr0s->len, ==, 1);
	tmp = g_ptr_array_index (pcr0s, 0);
	g_assert_cmpstr (tmp, ==, "ebead4b31c7c49e193c440cd6ee90bc1b61a3ca6");
}

int
main (int argc, char **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_log_set_fatal_mask (NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);
	g_test_add_func ("/tpm-eventlog/parse{v1}", fu_test_tpm_eventlog_parse_v1_func);
	g_test_add_func ("/tpm-eventlog/parse{v2}", fu_test_tpm_eventlog_parse_v2_func);
	return g_test_run ();
}
