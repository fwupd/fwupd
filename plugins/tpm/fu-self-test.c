/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-context-private.h"
#include "fu-plugin-private.h"
#include "fu-security-attrs-private.h"
#include "fu-tpm-plugin.h"
#include "fu-tpm-v1-device.h"
#include "fu-tpm-v2-device.h"

static void
fu_tpm_device_1_2_func(void)
{
	FuTpmDevice *device;
	GPtrArray *devices;
	gboolean ret;
	g_autofree gchar *testdatadir = NULL;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(FwupdSecurityAttr) attr0 = NULL;
	g_autoptr(FwupdSecurityAttr) attr1 = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) pcr0s = NULL;
	g_autoptr(GPtrArray) pcrs = NULL;
	const gchar *tpm_server_running = g_getenv("TPM2TOOLS_TCTI");

	if (tpm_server_running != NULL) {
		g_test_skip("Skipping TPM1.2 tests when simulator running");
		return;
	}

	/* set up test harness */
	testdatadir = g_test_build_filename(G_TEST_DIST, "tests", NULL);
	fu_context_set_path(ctx, FU_PATH_KIND_SYSFSDIR_TPM, testdatadir);

	/* do not save silo */
	ret = fu_context_load_quirks(ctx, FU_QUIRKS_LOAD_FLAG_NO_CACHE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* load the plugin */
	plugin = fu_plugin_new_from_gtype(fu_tpm_plugin_get_type(), ctx);
	ret = fu_plugin_runner_startup(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_plugin_runner_coldplug(plugin, progress, &error);
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
	pcrs = fu_tpm_device_get_checksums(device, 999);
	g_assert_nonnull(pcrs);
	g_assert_cmpint(pcrs->len, ==, 0);

	/* verify HSI attributes */
	fu_plugin_runner_add_security_attrs(plugin, attrs);
	attr0 = fu_security_attrs_get_by_appstream_id(attrs,
						      FWUPD_SECURITY_ATTR_ID_TPM_VERSION_20,
						      &error);
	g_assert_no_error(error);
	g_assert_nonnull(attr0);
	g_assert_cmpint(fwupd_security_attr_get_result(attr0),
			==,
			FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);

	attr1 = fu_security_attrs_get_by_appstream_id(attrs,
						      FWUPD_SECURITY_ATTR_ID_TPM_EMPTY_PCR,
						      &error);
	g_assert_no_error(error);
	g_assert_nonnull(attr1);
	/* some PCRs are empty, but PCRs 0-7 are set (tests/tpm0/pcrs) */
	g_assert_cmpint(fwupd_security_attr_get_result(attr1),
			==,
			FWUPD_SECURITY_ATTR_RESULT_VALID);
}

static void
fu_tpm_device_2_0_func(void)
{
	gboolean ret;
	FuTpmV2Device *device;
	GPtrArray *devices;
	const gchar *tpm_server_running = g_getenv("TPM2TOOLS_TCTI");
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) pcr0s = NULL;
	g_autoptr(GPtrArray) pcrs = NULL;

	if (tpm_server_running == NULL) {
		g_test_skip("TPM2.0 tests require simulated TPM2.0 running");
		return;
	}

	/* do not save silo */
	ret = fu_context_load_quirks(ctx, FU_QUIRKS_LOAD_FLAG_NO_CACHE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* load the plugin */
	plugin = fu_plugin_new_from_gtype(fu_tpm_plugin_get_type(), ctx);
	ret = fu_plugin_runner_startup(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	ret = fu_plugin_runner_coldplug(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* get the v2.0 device */
	devices = fu_plugin_get_devices(plugin);
	g_assert_cmpint(devices->len, ==, 1);
	device = g_ptr_array_index(devices, 0);
	g_assert_true(FU_IS_TPM_V2_DEVICE(device));

	pcr0s = fu_tpm_device_get_checksums(FU_TPM_DEVICE(device), 0);
	g_assert_nonnull(pcr0s);
	g_assert_cmpint(pcr0s->len, >=, 1);
	pcrs = fu_tpm_device_get_checksums(FU_TPM_DEVICE(device), 999);
	g_assert_nonnull(pcrs);
	g_assert_cmpint(pcrs->len, ==, 0);
}

static void
fu_tpm_eventlog_parse_v1_func(void)
{
	const gchar *tmp;
	gboolean ret;
	g_autofree gchar *fn = NULL;
	g_autoptr(FuTpmEventlog) eventlog = fu_tpm_eventlog_v1_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) pcr0s = NULL;
	g_autoptr(GBytes) blob = NULL;

	fn = g_test_build_filename(G_TEST_DIST, "tests", "binary_bios_measurements-v1", NULL);
	if (!g_file_test(fn, G_FILE_TEST_EXISTS)) {
		g_test_skip("Missing binary_bios_measurements-v1");
		return;
	}
	blob = fu_bytes_get_contents(fn, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob);
	ret = fu_firmware_parse_bytes(FU_FIRMWARE(eventlog),
				      blob,
				      0x0,
				      FU_FIRMWARE_PARSE_FLAG_NONE,
				      &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	pcr0s = fu_tpm_eventlog_calc_checksums(eventlog, 0, &error);
	g_assert_no_error(error);
	g_assert_nonnull(pcr0s);
	g_assert_cmpint(pcr0s->len, ==, 1);
	tmp = g_ptr_array_index(pcr0s, 0);
	g_assert_cmpstr(tmp, ==, "543ae96e57b6fc4003531cd0dab1d9ba7f8166e0");
}

static void
fu_tpm_eventlog_parse_v2_func(void)
{
	const gchar *tmp;
	gboolean ret;
	g_autofree gchar *fn = NULL;
	g_autoptr(FuTpmEventlog) eventlog = fu_tpm_eventlog_v2_new();
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) pcr0s = NULL;

	fn = g_test_build_filename(G_TEST_DIST, "tests", "binary_bios_measurements-v2", NULL);
	if (!g_file_test(fn, G_FILE_TEST_EXISTS)) {
		g_test_skip("Missing binary_bios_measurements-v2");
		return;
	}
	blob = fu_bytes_get_contents(fn, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob);
	ret = fu_firmware_parse_bytes(FU_FIRMWARE(eventlog),
				      blob,
				      0x0,
				      FU_FIRMWARE_PARSE_FLAG_NONE,
				      &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	pcr0s = fu_tpm_eventlog_calc_checksums(eventlog, 0, &error);
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
	g_autofree gchar *testdatadir = NULL;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GError) error = NULL;
	const gchar *tpm_server_running = g_getenv("TPM2TOOLS_TCTI");

	if (tpm_server_running != NULL) {
		g_test_skip("Skipping empty PCR tests when simulator running");
		return;
	}

	/* do not save silo */
	ret = fu_context_load_quirks(ctx, FU_QUIRKS_LOAD_FLAG_NO_CACHE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* set up test harness */
	testdatadir = g_test_build_filename(G_TEST_DIST, "tests", "empty_pcr", NULL);
	fu_context_set_path(ctx, FU_PATH_KIND_SYSFSDIR_TPM, testdatadir);

	/* load the plugin */
	plugin = fu_plugin_new_from_gtype(fu_tpm_plugin_get_type(), ctx);
	ret = fu_plugin_runner_startup(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_plugin_runner_coldplug(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* verify HSI attr */
	fu_plugin_runner_add_security_attrs(plugin, attrs);
	attr = fu_security_attrs_get_by_appstream_id(attrs,
						     FWUPD_SECURITY_ATTR_ID_TPM_EMPTY_PCR,
						     &error);
	g_assert_no_error(error);
	g_assert_nonnull(attr);
	/* PCR 6 is empty (tests/empty_pcr/tpm0/pcrs) */
	g_assert_cmpint(fwupd_security_attr_get_result(attr),
			==,
			FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	(void)g_setenv("FWUPD_UEFI_TEST", "1", TRUE);
	(void)g_setenv("G_MESSAGES_DEBUG", "all", TRUE);

	/* tests go here */
	g_test_add_func("/tpm/pcrs1.2", fu_tpm_device_1_2_func);
	g_test_add_func("/tpm/pcrs2.0", fu_tpm_device_2_0_func);
	g_test_add_func("/tpm/empty-pcr", fu_tpm_empty_pcr_func);
	g_test_add_func("/tpm/eventlog-parse{v1}", fu_tpm_eventlog_parse_v1_func);
	g_test_add_func("/tpm/eventlog-parse{v2}", fu_tpm_eventlog_parse_v2_func);
	return g_test_run();
}
