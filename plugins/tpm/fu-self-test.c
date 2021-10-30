/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-context-private.h"
#include "fu-tpm-v1-device.h"
#include "fu-tpm-v2-device.h"

static void
fu_tpm_device_1_2_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuTpmDevice) device = fu_tpm_v1_device_new(ctx);
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) pcr0s = NULL;
	g_autoptr(GPtrArray) pcrXs = NULL;
	g_autofree gchar *testdatadir = NULL;

	testdatadir = g_test_build_filename(G_TEST_DIST, "tests", "tpm0", "pcrs", NULL);
	g_object_set(device, "device-file", testdatadir, NULL);

	ret = fu_device_setup(FU_DEVICE(device), &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	pcr0s = fu_tpm_device_get_checksums(device, 0);
	g_assert_nonnull(pcr0s);
	g_assert_cmpint(pcr0s->len, ==, 1);
	pcrXs = fu_tpm_device_get_checksums(device, 999);
	g_assert_nonnull(pcrXs);
	g_assert_cmpint(pcrXs->len, ==, 0);
}

static void
fu_tpm_device_2_0_func(void)
{
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuTpmDevice) device = fu_tpm_v2_device_new(ctx);
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) pcr0s = NULL;
	g_autoptr(GPtrArray) pcrXs = NULL;
	const gchar *tpm_server_running = g_getenv("TPM_SERVER_RUNNING");
	g_setenv("FWUPD_FORCE_TPM2", "1", TRUE);

#ifdef HAVE_GETUID
	if (tpm_server_running == NULL && (getuid() != 0 || geteuid() != 0)) {
		g_test_skip("TPM2.0 tests require simulated TPM2.0 running or need root access "
			    "with physical TPM");
		return;
	}
#endif

	if (!fu_device_setup(FU_DEVICE(device), &error)) {
		if (tpm_server_running == NULL &&
		    g_error_matches(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND)) {
			g_test_skip("no physical or simulated TPM 2.0 device available");
			return;
		}
	}
	g_assert_no_error(error);
	pcr0s = fu_tpm_device_get_checksums(device, 0);
	g_assert_nonnull(pcr0s);
	g_assert_cmpint(pcr0s->len, >=, 1);
	pcrXs = fu_tpm_device_get_checksums(device, 999);
	g_assert_nonnull(pcrXs);
	g_assert_cmpint(pcrXs->len, ==, 0);
	g_unsetenv("FWUPD_FORCE_TPM2");
}

int
main(int argc, char **argv)
{
	g_autofree gchar *testdatadir = NULL;

	g_test_init(&argc, &argv, NULL);

	testdatadir = g_test_build_filename(G_TEST_DIST, "tests", NULL);
	g_setenv("FWUPD_SYSFSFWDIR", testdatadir, TRUE);
	g_setenv("FWUPD_SYSFSDRIVERDIR", testdatadir, TRUE);
	g_setenv("FWUPD_UEFI_TEST", "1", TRUE);

	/* only critical and error are fatal */
	g_log_set_fatal_mask(NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);
	g_setenv("G_MESSAGES_DEBUG", "all", TRUE);

	/* tests go here */
	g_test_add_func("/tpm/pcrs1.2", fu_tpm_device_1_2_func);
	g_test_add_func("/tpm/pcrs2.0", fu_tpm_device_2_0_func);
	return g_test_run();
}
