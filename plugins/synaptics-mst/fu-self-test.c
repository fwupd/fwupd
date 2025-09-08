/*
 * Copyright 2017 Mario Limonciello <mario.limonciello@dell.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <glib/gstdio.h>
#include <stdlib.h>

#include "fu-context-private.h"
#include "fu-plugin-private.h"
#include "fu-synaptics-mst-common.h"
#include "fu-synaptics-mst-device.h"
#include "fu-synaptics-mst-firmware.h"
#include "fu-synaptics-mst-plugin.h"

static void
fu_test_plugin_device_added_cb(FuPlugin *plugin, FuDevice *device, gpointer user_data)
{
	GPtrArray **devices = (GPtrArray **)user_data;
	g_ptr_array_add(*devices, g_object_ref(device));
}

static void
fu_test_add_fake_devices_from_dir(FuPlugin *plugin, const gchar *path)
{
	const gchar *basename;
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;
	g_autoptr(GDir) dir = g_dir_open(path, 0, &error);
	g_assert_no_error(error);
	g_assert_nonnull(dir);

	ret = fu_context_load_quirks(ctx, FU_QUIRKS_LOAD_FLAG_NO_CACHE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_context_load_hwinfo(ctx, progress, FU_CONTEXT_HWID_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	while ((basename = g_dir_read_name(dir)) != NULL) {
		g_autofree gchar *fn = g_build_filename(path, basename, NULL);
		g_autoptr(FuDeviceLocker) locker = NULL;
		g_autoptr(FuProgress) progress_local = fu_progress_new(G_STRLOC);
		g_autoptr(FuSynapticsMstDevice) dev = NULL;
		g_autoptr(GError) error_local = NULL;

		if (!g_str_has_prefix(basename, "drm_dp_aux"))
			continue;
		dev = g_object_new(FU_TYPE_SYNAPTICS_MST_DEVICE,
				   "context",
				   ctx,
				   "physical-id",
				   "PCI_SLOT_NAME=0000:3e:00.0",
				   "logical-id",
				   basename,
				   "subsystem",
				   "drm_dp_aux_dev",
				   "device-file",
				   fn,
				   "dpcd-ieee-oui",
				   SYNAPTICS_IEEE_OUI,
				   NULL);
		fu_device_add_private_flag(FU_DEVICE(dev),
					   FU_SYNAPTICS_MST_DEVICE_FLAG_IS_SOMEWHAT_EMULATED);
		g_debug("creating drm_dp_aux_dev object backed by %s", fn);
		locker = fu_device_locker_new(FU_DEVICE(dev), &error_local);
		if (locker == NULL) {
			g_debug("%s", error_local->message);
			continue;
		}
		fu_plugin_device_add(plugin, FU_DEVICE(dev));
	}
}

/* test with no Synaptics MST devices */
static void
fu_plugin_synaptics_mst_none_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) devices =
	    g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	g_autofree gchar *filename = NULL;

	ret = fu_context_load_quirks(ctx, FU_QUIRKS_LOAD_FLAG_NO_CACHE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_context_load_hwinfo(ctx, progress, FU_CONTEXT_HWID_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	plugin = fu_plugin_new_from_gtype(fu_synaptics_mst_plugin_get_type(), ctx);
	g_signal_connect(FU_PLUGIN(plugin),
			 "device-added",
			 G_CALLBACK(fu_test_plugin_device_added_cb),
			 &devices);
	ret = fu_plugin_runner_startup(plugin, progress, &error);
	if (!ret && g_error_matches(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED)) {
		g_test_skip("Skipping tests due to unsupported configuration");
		return;
	}
	g_assert_no_error(error);
	g_assert_true(ret);

	filename = g_test_build_filename(G_TEST_DIST, "tests", "no_devices", NULL);
	if (!g_file_test(filename, G_FILE_TEST_EXISTS)) {
		g_test_skip("Missing no_devices");
		return;
	}
	fu_test_add_fake_devices_from_dir(plugin, filename);
	g_assert_cmpint(devices->len, ==, 0);
}

/* emulate adding/removing a Dell TB16 dock */
static void
fu_plugin_synaptics_mst_tb16_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) devices =
	    g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	g_autofree gchar *filename = NULL;

	ret = fu_context_load_quirks(ctx, FU_QUIRKS_LOAD_FLAG_NO_CACHE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_context_load_hwinfo(ctx, progress, FU_CONTEXT_HWID_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	plugin = fu_plugin_new_from_gtype(fu_synaptics_mst_plugin_get_type(), ctx);
	g_signal_connect(FU_PLUGIN(plugin),
			 "device-added",
			 G_CALLBACK(fu_test_plugin_device_added_cb),
			 &devices);

	ret = fu_plugin_runner_startup(plugin, progress, &error);
	if (!ret && g_error_matches(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED)) {
		g_test_skip("Skipping tests due to unsupported configuration");
		return;
	}
	g_assert_no_error(error);
	g_assert_true(ret);

	filename = g_test_build_filename(G_TEST_DIST, "tests", "tb16_dock", NULL);
	if (!g_file_test(filename, G_FILE_TEST_EXISTS)) {
		g_test_skip("Missing tb16_dock");
		return;
	}
	fu_test_add_fake_devices_from_dir(plugin, filename);
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device = g_ptr_array_index(devices, i);
		g_autofree gchar *tmp = fu_device_to_string(device);
		g_debug("%s", tmp);
	}
	g_assert_cmpint(devices->len, ==, 2);
}

static void
fu_synaptics_mst_firmware_xml_func(void)
{
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autofree gchar *csum1 = NULL;
	g_autofree gchar *csum2 = NULL;
	g_autofree gchar *xml_out = NULL;
	g_autofree gchar *xml_src = NULL;
	g_autoptr(FuFirmware) firmware1 = fu_synaptics_mst_firmware_new();
	g_autoptr(FuFirmware) firmware2 = fu_synaptics_mst_firmware_new();
	g_autoptr(GError) error = NULL;

	/* build and write */
	filename = g_test_build_filename(G_TEST_DIST, "tests", "synaptics-mst.builder.xml", NULL);
	ret = g_file_get_contents(filename, &xml_src, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_firmware_build_from_xml(firmware1, xml_src, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	csum1 = fu_firmware_get_checksum(firmware1, G_CHECKSUM_SHA1, &error);
	g_assert_no_error(error);
	g_assert_cmpstr(csum1, ==, "67b8fc4661f7585a8cd6c46ef6088293d4399135");

	/* ensure we can round-trip */
	xml_out = fu_firmware_export_to_xml(firmware1, FU_FIRMWARE_EXPORT_FLAG_NONE, &error);
	g_assert_no_error(error);
	ret = fu_firmware_build_from_xml(firmware2, xml_out, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	csum2 = fu_firmware_get_checksum(firmware2, G_CHECKSUM_SHA1, &error);
	g_assert_cmpstr(csum1, ==, csum2);
}

int
main(int argc, char **argv)
{
	g_autofree gchar *testdatadir = NULL;
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);

	/* only critical and error are fatal */
	g_log_set_fatal_mask(NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);
	testdatadir = g_test_build_filename(G_TEST_DIST, "tests", NULL);
	(void)g_setenv("FWUPD_SYSFSFWDIR", testdatadir, TRUE);
	(void)g_setenv("FWUPD_SYSFSFWATTRIBDIR", testdatadir, TRUE);
	(void)g_setenv("CONFIGURATION_DIRECTORY", testdatadir, TRUE);

	g_assert_cmpint(g_mkdir_with_parents("/tmp/fwupd-self-test/var/lib/fwupd", 0755), ==, 0);

	/* tests go here */
	g_test_add_func("/fwupd/plugin/synaptics_mst{none}", fu_plugin_synaptics_mst_none_func);
	g_test_add_func("/fwupd/plugin/synaptics_mst{tb16}", fu_plugin_synaptics_mst_tb16_func);
	g_test_add_func("/fwupd/plugin/synaptics_mst/firmware{xml}",
			fu_synaptics_mst_firmware_xml_func);

	return g_test_run();
}
