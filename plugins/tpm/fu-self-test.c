/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-context-private.h"
#include "fu-plugin-private.h"
#include "fu-security-attrs-private.h"
#include "fu-tpm-eventlog-common.h"
#include "fu-tpm-eventlog-parser.h"
#include "fu-tpm-v1-device.h"
#include "fu-tpm-v2-device.h"

static void
fu_tpm_device_1_2_func(void)
{
	FuTpmDevice *device;
	GPtrArray *devices;
	gboolean ret;
	g_autofree gchar *pluginfn = NULL;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuPlugin) plugin = fu_plugin_new(ctx);
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(FwupdSecurityAttr) attr0 = NULL;
	g_autoptr(FwupdSecurityAttr) attr1 = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) pcr0s = NULL;
	g_autoptr(GPtrArray) pcrXs = NULL;

	/* do not save silo */
	ret = fu_context_load_quirks(ctx, FU_QUIRKS_LOAD_FLAG_NO_CACHE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* load the plugin */
	pluginfn = g_test_build_filename(G_TEST_BUILT, "libfu_plugin_tpm." G_MODULE_SUFFIX, NULL);
	ret = fu_plugin_open(plugin, pluginfn, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_plugin_runner_startup(plugin, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* get the v1.2 device */
	devices = fu_plugin_get_devices(plugin);
	g_assert_cmpint(devices->len, ==, 1);
	device = g_ptr_array_index(devices, 0);
	g_assert_true(FU_IS_TPM_DEVICE(device));

	/* verify checksums set correctly */
	pcr0s = fu_tpm_device_get_checksums(device, 0);
	g_assert_nonnull(pcr0s);
	g_assert_cmpint(pcr0s->len, ==, 1);
	pcrXs = fu_tpm_device_get_checksums(device, 999);
	g_assert_nonnull(pcrXs);
	g_assert_cmpint(pcrXs->len, ==, 0);

	/* verify HSI attributes */
	fu_plugin_runner_add_security_attrs(plugin, attrs);
	attr0 = fu_security_attrs_get_by_appstream_id(attrs, FWUPD_SECURITY_ATTR_ID_TPM_VERSION_20);
	g_assert_nonnull(attr0);
	g_assert_cmpint(fwupd_security_attr_get_result(attr0),
			==,
			FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);

	attr1 = fu_security_attrs_get_by_appstream_id(attrs, FWUPD_SECURITY_ATTR_ID_TPM_EMPTY_PCR);
	g_assert_nonnull(attr1);
	/* Some PCRs are empty, but PCRs 0-7 are set (tests/tpm0/pcrs) */
	g_assert_cmpint(fwupd_security_attr_get_result(attr1),
			==,
			FWUPD_SECURITY_ATTR_RESULT_VALID);
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
	(void)g_setenv("FWUPD_FORCE_TPM2", "1", TRUE);

#ifdef HAVE_GETUID
	if (tpm_server_running == NULL && (getuid() != 0 || geteuid() != 0)) {
		g_test_skip("TPM2.0 tests require simulated TPM2.0 running or need root access "
			    "with physical TPM");
		g_unsetenv("FWUPD_FORCE_TPM2");
		return;
	}
#endif

	if (!fu_device_setup(FU_DEVICE(device), &error)) {
		if (tpm_server_running == NULL &&
		    g_error_matches(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND)) {
			g_test_skip("no physical or simulated TPM 2.0 device available");
			g_unsetenv("FWUPD_FORCE_TPM2");
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

static void
fu_tpm_eventlog_parse_v1_func(void)
{
	const gchar *ci = g_getenv("CI_NETWORK");
	const gchar *tmp;
	gboolean ret;
	gsize bufsz = 0;
	g_autofree gchar *fn = NULL;
	g_autofree guint8 *buf = NULL;
	g_autoptr(GPtrArray) items = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) pcr0s = NULL;

	fn = g_test_build_filename(G_TEST_DIST, "tests", "binary_bios_measurements-v1", NULL);
	if (!g_file_test(fn, G_FILE_TEST_EXISTS) && ci == NULL) {
		g_test_skip("Missing binary_bios_measurements-v1");
		return;
	}
	ret = g_file_get_contents(fn, (gchar **)&buf, &bufsz, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	items = fu_tpm_eventlog_parser_new(buf, bufsz, FU_TPM_EVENTLOG_PARSER_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(items);

	pcr0s = fu_tpm_eventlog_calc_checksums(items, 0, &error);
	g_assert_no_error(error);
	g_assert_nonnull(pcr0s);
	g_assert_cmpint(pcr0s->len, ==, 1);
	tmp = g_ptr_array_index(pcr0s, 0);
	g_assert_cmpstr(tmp, ==, "543ae96e57b6fc4003531cd0dab1d9ba7f8166e0");
}

static void
fu_tpm_eventlog_parse_v2_func(void)
{
	const gchar *ci = g_getenv("CI_NETWORK");
	const gchar *tmp;
	gboolean ret;
	gsize bufsz = 0;
	g_autofree gchar *fn = NULL;
	g_autofree guint8 *buf = NULL;
	g_autoptr(GPtrArray) items = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) pcr0s = NULL;

	fn = g_test_build_filename(G_TEST_DIST, "tests", "binary_bios_measurements-v2", NULL);
	if (!g_file_test(fn, G_FILE_TEST_EXISTS) && ci == NULL) {
		g_test_skip("Missing binary_bios_measurements-v2");
		return;
	}
	ret = g_file_get_contents(fn, (gchar **)&buf, &bufsz, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	items = fu_tpm_eventlog_parser_new(buf, bufsz, FU_TPM_EVENTLOG_PARSER_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(items);

	pcr0s = fu_tpm_eventlog_calc_checksums(items, 0, &error);
	g_assert_no_error(error);
	g_assert_nonnull(pcr0s);
	g_assert_cmpint(pcr0s->len, ==, 2);
	tmp = g_ptr_array_index(pcr0s, 0);
	g_assert_cmpstr(tmp, ==, "ebead4b31c7c49e193c440cd6ee90bc1b61a3ca6");
	tmp = g_ptr_array_index(pcr0s, 1);
	g_assert_cmpstr(tmp,
			==,
			"6d9fed68092cfb91c9552bcb7879e75e1df36efd407af67690dc3389a5722fab");
}

static void
fu_tpm_empty_pcr_func(void)
{
	gboolean ret;
	g_autofree gchar *pluginfn = NULL;
	g_autofree gchar *testdatadir = NULL;
	g_auto(GStrv) environ_old = NULL;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuPlugin) plugin = fu_plugin_new(ctx);
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GError) error = NULL;

	/* do not save silo */
	ret = fu_context_load_quirks(ctx, FU_QUIRKS_LOAD_FLAG_NO_CACHE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* save environment and set broken PCR data */
	environ_old = g_get_environ();
	testdatadir = g_test_build_filename(G_TEST_DIST, "tests", "empty_pcr", NULL);
	(void)g_setenv("FWUPD_SYSFSTPMDIR", testdatadir, TRUE);

	/* load the plugin */
	pluginfn = g_test_build_filename(G_TEST_BUILT, "libfu_plugin_tpm." G_MODULE_SUFFIX, NULL);
	ret = fu_plugin_open(plugin, pluginfn, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_plugin_runner_startup(plugin, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* verify HSI attr */
	fu_plugin_runner_add_security_attrs(plugin, attrs);
	attr = fu_security_attrs_get_by_appstream_id(attrs, FWUPD_SECURITY_ATTR_ID_TPM_EMPTY_PCR);
	g_assert_nonnull(attr);
	/* PCR 6 is empty (tests/empty_pcr/tpm0/pcrs) */
	g_assert_cmpint(fwupd_security_attr_get_result(attr),
			==,
			FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);

	/* restore default environment */
	(void)g_setenv("FWUPD_SYSFSTPMDIR",
		       g_environ_getenv(environ_old, "FWUPD_SYSFSTPMDIR"),
		       TRUE);
}

int
main(int argc, char **argv)
{
	g_autofree gchar *testdatadir = NULL;

	g_test_init(&argc, &argv, NULL);

	testdatadir = g_test_build_filename(G_TEST_DIST, "tests", NULL);
	(void)g_setenv("FWUPD_SYSFSTPMDIR", testdatadir, TRUE);
	(void)g_setenv("FWUPD_UEFI_TEST", "1", TRUE);

	/* only critical and error are fatal */
	g_log_set_fatal_mask(NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);
	(void)g_setenv("G_MESSAGES_DEBUG", "all", TRUE);

	/* tests go here */
	g_test_add_func("/tpm/pcrs1.2", fu_tpm_device_1_2_func);
	g_test_add_func("/tpm/pcrs2.0", fu_tpm_device_2_0_func);
	g_test_add_func("/tpm/empty-pcr", fu_tpm_empty_pcr_func);
	g_test_add_func("/tpm/eventlog-parse{v1}", fu_tpm_eventlog_parse_v1_func);
	g_test_add_func("/tpm/eventlog-parse{v2}", fu_tpm_eventlog_parse_v2_func);
	return g_test_run();
}
