/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-context-private.h"
#include "fu-plugin-private.h"
#include "fu-security-attrs-private.h"
#include "fu-temporary-directory.h"
#include "fu-tpm-plugin.h"
#include "fu-tpm-struct.h"
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
	fu_context_add_flag(ctx, FU_CONTEXT_FLAG_NO_CACHE);
	ret = fu_context_load(ctx, progress, FU_CONTEXT_LOAD_FLAG_NONE, &error);
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
	fu_context_add_flag(ctx, FU_CONTEXT_FLAG_NO_CACHE);
	ret = fu_context_load(ctx, progress, FU_CONTEXT_LOAD_FLAG_NONE, &error);
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
	fu_context_add_flag(ctx, FU_CONTEXT_FLAG_NO_CACHE);
	ret = fu_context_load(ctx, progress, FU_CONTEXT_LOAD_FLAG_NONE, &error);
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

static GBytes *
fu_tpm_test_build_eventlog_v1(const gchar *data, gsize datasz)
{
	gboolean ret;
	guint8 digest[20] = {0};
	g_autoptr(FuStructTpmEventLog1Item) st = fu_struct_tpm_event_log1_item_new();

	fu_struct_tpm_event_log1_item_set_pcr(st, 0);
	fu_struct_tpm_event_log1_item_set_type(st, FU_TPM_EVENTLOG_ITEM_KIND_POST_CODE);
	ret = fu_struct_tpm_event_log1_item_set_digest(st, digest, sizeof(digest), NULL);
	g_assert_true(ret);
	fu_struct_tpm_event_log1_item_set_datasz(st, datasz);
	g_byte_array_append(st->buf, (const guint8 *)data, datasz);
	return g_byte_array_free_to_bytes(g_byte_array_ref(st->buf));
}

static void
fu_tpm_coreboot_vboot_not_found_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GError) error = NULL;
	FuHwids *hwids = fu_context_get_hwids(ctx);
	const gchar *tpm_server_running = g_getenv("TPM2TOOLS_TCTI");

	if (tpm_server_running != NULL) {
		g_test_skip("Skipping coreboot vboot tests when simulator running");
		return;
	}

	/* set up test harness */
	tmpdir = fu_temporary_directory_new("tpm-no-eventlog", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);
	fu_context_set_tmpdir(ctx, FU_PATH_KIND_SYSFSDIR, tmpdir);
	fu_context_set_tmpdir(ctx, FU_PATH_KIND_SYSFSDIR_TPM, tmpdir);

	fu_context_add_flag(ctx, FU_CONTEXT_FLAG_NO_CACHE);
	fu_context_add_flag(ctx, FU_CONTEXT_FLAG_LOADED_HWINFO);
	fu_hwids_add_value(hwids, FU_HWIDS_KEY_BIOS_VENDOR, "coreboot");

	ret = fu_context_load(ctx, progress, FU_CONTEXT_LOAD_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* load the plugin -- no eventlog file exists */
	plugin = fu_plugin_new_from_gtype(fu_tpm_plugin_get_type(), ctx);
	ret = fu_plugin_runner_startup(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_plugin_runner_coldplug(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* verify the vboot attr is NOT_FOUND since no eventlog exists */
	fu_plugin_runner_add_security_attrs(plugin, attrs);
	attr = fu_security_attrs_get_by_appstream_id(attrs,
						     FWUPD_SECURITY_ATTR_ID_COREBOOT_VBOOT,
						     &error);
	g_assert_no_error(error);
	g_assert_nonnull(attr);
	g_assert_cmpint(fwupd_security_attr_get_result(attr),
			==,
			FWUPD_SECURITY_ATTR_RESULT_NOT_FOUND);
}

static void
fu_tpm_coreboot_vboot_enabled_func(void)
{
	gboolean ret;
	g_autofree gchar *testdatadir = NULL;
	g_autofree gchar *eventlog_dir = NULL;
	g_autofree gchar *eventlog_fn = NULL;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GBytes) eventlog_blob = NULL;
	g_autoptr(GError) error = NULL;
	FuHwids *hwids = fu_context_get_hwids(ctx);
	const gchar *vboot_data = "VBOOT:test";
	const gchar *tpm_server_running = g_getenv("TPM2TOOLS_TCTI");

	if (tpm_server_running != NULL) {
		g_test_skip("Skipping coreboot vboot tests when simulator running");
		return;
	}

	/* set up test harness */
	testdatadir = g_test_build_filename(G_TEST_DIST, "tests", NULL);
	fu_context_set_path(ctx, FU_PATH_KIND_SYSFSDIR_TPM, testdatadir);

	/* build a v1 eventlog with a VBOOT: entry in a temporary directory */
	tmpdir = fu_temporary_directory_new("tpm-vboot", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);
	eventlog_dir = fu_temporary_directory_build(tmpdir, "kernel", "security", "tpm0", NULL);
	g_assert_cmpint(g_mkdir_with_parents(eventlog_dir, 0700), ==, 0);
	eventlog_fn = g_build_filename(eventlog_dir, "binary_bios_measurements", NULL);
	eventlog_blob = fu_tpm_test_build_eventlog_v1(vboot_data, strlen(vboot_data));
	ret = fu_bytes_set_contents(eventlog_fn, eventlog_blob, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	fu_context_set_path(ctx, FU_PATH_KIND_SYSFSDIR, fu_temporary_directory_get_path(tmpdir));

	fu_context_add_flag(ctx, FU_CONTEXT_FLAG_NO_CACHE);
	fu_context_add_flag(ctx, FU_CONTEXT_FLAG_LOADED_HWINFO);
	fu_hwids_add_value(hwids, FU_HWIDS_KEY_BIOS_VENDOR, "coreboot");

	ret = fu_context_load(ctx, progress, FU_CONTEXT_LOAD_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* load the plugin -- eventlog exists with VBOOT: entry */
	plugin = fu_plugin_new_from_gtype(fu_tpm_plugin_get_type(), ctx);
	ret = fu_plugin_runner_startup(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_plugin_runner_coldplug(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* verify the vboot attr is SUCCESS */
	fu_plugin_runner_add_security_attrs(plugin, attrs);
	attr = fu_security_attrs_get_by_appstream_id(attrs,
						     FWUPD_SECURITY_ATTR_ID_COREBOOT_VBOOT,
						     &error);
	g_assert_no_error(error);
	g_assert_nonnull(attr);
	g_assert_cmpint(fwupd_security_attr_get_result(attr),
			==,
			FWUPD_SECURITY_ATTR_RESULT_ENABLED);
	g_assert_true(fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS));
}

static void
fu_tpm_coreboot_vboot_not_enabled_func(void)
{
	gboolean ret;
	g_autofree gchar *testdatadir = NULL;
	g_autofree gchar *eventlog_dir = NULL;
	g_autofree gchar *eventlog_fn = NULL;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuSecurityAttrs) attrs = fu_security_attrs_new();
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	g_autoptr(GBytes) eventlog_blob = NULL;
	g_autoptr(GError) error = NULL;
	FuHwids *hwids = fu_context_get_hwids(ctx);
	const gchar *other_data = "OTHER:data";
	const gchar *tpm_server_running = g_getenv("TPM2TOOLS_TCTI");

	if (tpm_server_running != NULL) {
		g_test_skip("Skipping coreboot vboot tests when simulator running");
		return;
	}

	/* set up test harness */
	testdatadir = g_test_build_filename(G_TEST_DIST, "tests", NULL);
	fu_context_set_path(ctx, FU_PATH_KIND_SYSFSDIR_TPM, testdatadir);

	/* build a v1 eventlog without a VBOOT: entry */
	tmpdir = fu_temporary_directory_new("tpm-no-vboot", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);
	eventlog_dir = fu_temporary_directory_build(tmpdir, "kernel", "security", "tpm0", NULL);
	g_assert_cmpint(g_mkdir_with_parents(eventlog_dir, 0700), ==, 0);
	eventlog_fn = g_build_filename(eventlog_dir, "binary_bios_measurements", NULL);
	eventlog_blob = fu_tpm_test_build_eventlog_v1(other_data, strlen(other_data));
	ret = fu_bytes_set_contents(eventlog_fn, eventlog_blob, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	fu_context_set_path(ctx, FU_PATH_KIND_SYSFSDIR, fu_temporary_directory_get_path(tmpdir));

	fu_context_add_flag(ctx, FU_CONTEXT_FLAG_NO_CACHE);
	fu_context_add_flag(ctx, FU_CONTEXT_FLAG_LOADED_HWINFO);
	fu_hwids_add_value(hwids, FU_HWIDS_KEY_BIOS_VENDOR, "coreboot");

	ret = fu_context_load(ctx, progress, FU_CONTEXT_LOAD_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* load the plugin -- eventlog exists but no VBOOT: entry */
	plugin = fu_plugin_new_from_gtype(fu_tpm_plugin_get_type(), ctx);
	ret = fu_plugin_runner_startup(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_plugin_runner_coldplug(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* verify the vboot attr is NOT_ENABLED */
	fu_plugin_runner_add_security_attrs(plugin, attrs);
	attr = fu_security_attrs_get_by_appstream_id(attrs,
						     FWUPD_SECURITY_ATTR_ID_COREBOOT_VBOOT,
						     &error);
	g_assert_no_error(error);
	g_assert_nonnull(attr);
	g_assert_cmpint(fwupd_security_attr_get_result(attr),
			==,
			FWUPD_SECURITY_ATTR_RESULT_NOT_ENABLED);
	g_assert_true(
	    fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONFIG_FW));
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/tpm/pcrs1.2", fu_tpm_device_1_2_func);
	g_test_add_func("/tpm/pcrs2.0", fu_tpm_device_2_0_func);
	g_test_add_func("/tpm/empty-pcr", fu_tpm_empty_pcr_func);
	g_test_add_func("/tpm/eventlog-parse/v1", fu_tpm_eventlog_parse_v1_func);
	g_test_add_func("/tpm/eventlog-parse/v2", fu_tpm_eventlog_parse_v2_func);
	g_test_add_func("/tpm/coreboot-vboot-not-found", fu_tpm_coreboot_vboot_not_found_func);
	g_test_add_func("/tpm/coreboot-vboot-enabled", fu_tpm_coreboot_vboot_enabled_func);
	g_test_add_func("/tpm/coreboot-vboot-not-enabled", fu_tpm_coreboot_vboot_not_enabled_func);
	return g_test_run();
}
