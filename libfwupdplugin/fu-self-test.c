/*
 * Copyright 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include <glib/gstdio.h>
#include <string.h>

#include "fwupd-enums-private.h"
#include "fwupd-security-attr-private.h"

#include "fu-bios-settings-private.h"
#include "fu-cab-firmware-private.h"
#include "fu-config-private.h"
#include "fu-context-private.h"
#include "fu-device-event-private.h"
#include "fu-device-private.h"
#include "fu-device-progress.h"
#include "fu-dummy-efivars.h"
#include "fu-efi-lz77-decompressor.h"
#include "fu-efi-x509-signature-private.h"
#include "fu-efivars-private.h"
#include "fu-kernel-search-path-private.h"
#include "fu-plugin-private.h"
#include "fu-progress-private.h"
#include "fu-security-attrs-private.h"
#include "fu-self-test-device.h"
#include "fu-self-test-struct.h"
#include "fu-smbios-private.h"
#include "fu-test.h"
#include "fu-udev-device-private.h"
#include "fu-volume-private.h"

/* nocheck:magic-inlines=300 */

static void
fu_archive_invalid_func(void)
{
	g_autofree gchar *filename = NULL;
	g_autoptr(FuArchive) archive = NULL;
	g_autoptr(GBytes) data = NULL;
	g_autoptr(GError) error = NULL;

#ifndef HAVE_LIBARCHIVE
	g_test_skip("no libarchive support");
	return;
#endif

	filename = g_test_build_filename(G_TEST_DIST, "tests", "metadata.xml", NULL);
	data = fu_bytes_get_contents(filename, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data);

	archive = fu_archive_new(data, FU_FIRMWARE_PARSE_FLAG_NONE, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_null(archive);
}

static void
fu_archive_cab_func(void)
{
	g_autofree gchar *checksum1 = NULL;
	g_autofree gchar *checksum2 = NULL;
	g_autofree gchar *filename = NULL;
	g_autoptr(FuArchive) archive = NULL;
	g_autoptr(GBytes) data = NULL;
	g_autoptr(GBytes) data_tmp1 = NULL;
	g_autoptr(GBytes) data_tmp2 = NULL;
	g_autoptr(GBytes) data_tmp3 = NULL;
	g_autoptr(GError) error = NULL;

#ifndef HAVE_LIBARCHIVE
	g_test_skip("no libarchive support");
	return;
#endif

	filename = g_test_build_filename(G_TEST_BUILT,
					 "tests",
					 "colorhug",
					 "colorhug-als-3.0.2.cab",
					 NULL);
	data = fu_bytes_get_contents(filename, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data);

	archive = fu_archive_new(data, FU_FIRMWARE_PARSE_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(archive);

	data_tmp1 = fu_archive_lookup_by_fn(archive, "firmware.metainfo.xml", &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_tmp1);
	checksum1 = g_compute_checksum_for_bytes(G_CHECKSUM_SHA1, data_tmp1);
	g_assert_cmpstr(checksum1, ==, "f62ee340c27bbb80229c3dd3cb2e78bddfc82d4f");

	data_tmp2 = fu_archive_lookup_by_fn(archive, "firmware.txt", &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_tmp2);
	checksum2 = g_compute_checksum_for_bytes(G_CHECKSUM_SHA1, data_tmp2);
	g_assert_cmpstr(checksum2, ==, "22596363b3de40b06f981fb85d82312e8c0ed511");

	data_tmp3 = fu_archive_lookup_by_fn(archive, "NOTGOINGTOEXIST.xml", &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_null(data_tmp3);
}

static void
fu_test_plugin_device_added_cb(FuPlugin *plugin, FuDevice *device, gpointer user_data)
{
	FuDevice **dev = (FuDevice **)user_data;
	*dev = g_object_ref(device);
	fu_test_loop_quit();
}

static void
fu_plugin_config_func(void)
{
	GStatBuf statbuf = {0};
	gboolean ret;
	gint rc;
	g_autofree gchar *fn = NULL;
	g_autofree gchar *value = NULL;
	g_autofree gchar *value_missing = NULL;
	g_autofree gchar *fn_mut = NULL;
	g_autofree gchar *sysconfdir_imu = NULL;
	g_autofree gchar *sysconfdir_mut = NULL;
	g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuPlugin) plugin = fu_plugin_new(ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;

#ifdef _WIN32
	/* the Windows file permission model is different than a simple octal value */
	g_test_skip("chmod not supported on Windows");
	return;
#endif

	/* set up test harness */
	tmpdir = fu_temporary_directory_new("config", &error);
	g_assert_no_error(error);
	g_assert_nonnull(tmpdir);
	sysconfdir_imu = fu_temporary_directory_build(tmpdir, "etc", "fwupd", NULL);
	fu_context_set_path(ctx, FU_PATH_KIND_SYSCONFDIR_PKG, sysconfdir_imu);
	sysconfdir_mut = fu_temporary_directory_build(tmpdir, "var", "etc", "fwupd", NULL);
	fu_context_set_path(ctx, FU_PATH_KIND_LOCALCONFDIR_PKG, sysconfdir_mut);

	fu_plugin_set_name(plugin, "test");
	fn =
	    fu_context_build_filename(ctx, &error, FU_PATH_KIND_SYSCONFDIR_PKG, "fwupd.conf", NULL);
	g_assert_no_error(error);
	g_assert_nonnull(fn);
	ret = fu_path_mkdir_parent(fn, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = g_file_set_contents(fn, "", -1, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* mutable file we'll be writing */
	fn_mut = g_build_filename(sysconfdir_mut, "fwupd.conf", NULL);
	g_assert_nonnull(fn_mut);
	ret = fu_path_mkdir_parent(fn_mut, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = g_file_set_contents(fn_mut, "", -1, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* load context */
	ret = fu_context_load_hwinfo(ctx, progress, FU_CONTEXT_HWID_FLAG_FIX_PERMISSIONS, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* set a value */
	ret = fu_plugin_set_config_value(plugin, "Key", "True", &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_true(g_file_test(fn, G_FILE_TEST_EXISTS));

	/* check it is only readable by the user/group */
	rc = g_stat(fn_mut, &statbuf);
	g_assert_cmpint(rc, ==, 0);
	g_assert_cmpint(statbuf.st_mode & 0777, ==, 0640);

	/* read back the value */
	fu_plugin_set_config_default(plugin, "NotGoingToExist", "Foo");
	value_missing = fu_plugin_get_config_value(plugin, "NotGoingToExist");
	g_assert_cmpstr(value_missing, ==, "Foo");
	value = fu_plugin_get_config_value(plugin, "Key");
	g_assert_cmpstr(value, ==, "True");
	g_assert_true(fu_plugin_get_config_value_boolean(plugin, "Key"));
}

static void
fu_plugin_devices_func(void)
{
	g_autoptr(FuDevice) device = fu_device_new(NULL);
	g_autoptr(FuDevice) child = fu_device_new(NULL);
	g_autoptr(FuPlugin) plugin = fu_plugin_new(NULL);
	GPtrArray *devices;

	devices = fu_plugin_get_devices(plugin);
	g_assert_nonnull(devices);
	g_assert_cmpint(devices->len, ==, 0);

	fu_device_set_id(device, "testdev");
	fu_device_set_name(device, "testdev");
	fu_plugin_device_add(plugin, device);
	g_assert_cmpint(devices->len, ==, 1);
	fu_plugin_device_remove(plugin, device);
	g_assert_cmpint(devices->len, ==, 0);

	/* add a child after adding the parent to the plugin */
	fu_device_set_id(child, "child");
	fu_device_set_name(child, "child");
	fu_device_add_child(device, child);
	g_assert_cmpint(devices->len, ==, 1);

	/* remove said child */
	fu_device_remove_child(device, child);
	g_assert_cmpint(devices->len, ==, 0);
}

static void
fu_plugin_delay_func(void)
{
	FuDevice *device_tmp;
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuDevice) device = NULL;

	plugin = fu_plugin_new(NULL);
	g_signal_connect(FU_PLUGIN(plugin),
			 "device-added",
			 G_CALLBACK(fu_test_plugin_device_added_cb),
			 &device_tmp);
	g_signal_connect(FU_PLUGIN(plugin),
			 "device-removed",
			 G_CALLBACK(fu_test_plugin_device_added_cb),
			 &device_tmp);

	/* add device straight away */
	device = fu_device_new(NULL);
	fu_device_set_id(device, "testdev");
	fu_plugin_device_add(plugin, device);
	g_assert_nonnull(device_tmp);
	g_assert_cmpstr(fu_device_get_id(device_tmp),
			==,
			"b7eccd0059d6d7dc2ef76c35d6de0048cc8c029d");
	g_clear_object(&device_tmp);

	/* remove device */
	fu_plugin_device_remove(plugin, device);
	g_assert_nonnull(device_tmp);
	g_assert_cmpstr(fu_device_get_id(device_tmp),
			==,
			"b7eccd0059d6d7dc2ef76c35d6de0048cc8c029d");
	g_clear_object(&device_tmp);
}

static void
fu_plugin_func(void)
{
	GHashTable *metadata;
	GPtrArray *rules;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuPlugin) plugin = fu_plugin_new(ctx);

	fu_plugin_add_rule(plugin, FU_PLUGIN_RULE_CONFLICTS, "dave1");
	fu_plugin_add_rule(plugin, FU_PLUGIN_RULE_CONFLICTS, "dave2");
	rules = fu_plugin_get_rules(plugin, FU_PLUGIN_RULE_CONFLICTS);
	g_assert_nonnull(rules);
	g_assert_cmpint(rules->len, ==, 2);
	rules = fu_plugin_get_rules(plugin, FU_PLUGIN_RULE_RUN_AFTER);
	g_assert_null(rules);

	fu_plugin_add_report_metadata(plugin, "key", "value");
	metadata = fu_plugin_get_report_metadata(plugin);
	g_assert_nonnull(metadata);
	g_assert_cmpint(g_hash_table_size(metadata), ==, 1);
}

static void
fu_plugin_vfuncs_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuPlugin) plugin = fu_plugin_new(ctx);
	g_autoptr(GError) error = NULL;

	/* nop: error */
	ret = fu_plugin_runner_modify_config(plugin, "foo", "bar", &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_false(ret);
	g_clear_error(&error);
}

static void
fu_plugin_device_gtype_func(void)
{
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuPlugin) plugin = fu_plugin_new(ctx);

	/* add the same gtype multiple times */
	fu_plugin_add_device_gtype(plugin, FU_TYPE_DEVICE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_DEVICE);
	g_assert_cmpint(fu_plugin_get_device_gtype_default(plugin), ==, FU_TYPE_DEVICE);

	/* now there's no explicit default */
	fu_plugin_add_device_gtype(plugin, FU_TYPE_SELF_TEST_DEVICE);
	g_assert_cmpint(fu_plugin_get_device_gtype_default(plugin), ==, G_TYPE_INVALID);

	/* make it explicit */
	fu_plugin_set_device_gtype_default(plugin, FU_TYPE_SELF_TEST_DEVICE);
	g_assert_cmpint(fu_plugin_get_device_gtype_default(plugin), ==, FU_TYPE_SELF_TEST_DEVICE);
}

static void
fu_plugin_backend_device_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuDevice) device = fu_device_new(ctx);
	g_autoptr(FuPlugin) plugin = fu_plugin_new(ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRFUNC);
	g_autoptr(GError) error = NULL;

	ret = fu_plugin_runner_backend_device_changed(plugin, device, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	fu_device_set_specialized_gtype(device, FU_TYPE_DEVICE);
	fu_device_add_private_flag(device, FU_DEVICE_PRIVATE_FLAG_ONLY_SUPPORTED);
	ret = fu_plugin_runner_backend_device_added(plugin, device, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_test_plugin_backend_proxy_device_added_cb(FuPlugin *plugin, FuDevice *device, gpointer user_data)
{
	FuDevice **dev = (FuDevice **)user_data;
	*dev = g_object_ref(device);
}

static void
fu_plugin_backend_proxy_device_func(void)
{
	gboolean ret;
	FuDevice *proxy;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuDevice) device = fu_device_new(ctx);
	g_autoptr(FuDevice) device_new = NULL;
	g_autoptr(FuPlugin) plugin = fu_plugin_new(ctx);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRFUNC);
	g_autoptr(GError) error = NULL;

	fu_device_set_id(device, "testdev");
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_EMULATED);
	ret = fu_plugin_runner_backend_device_changed(plugin, device, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* watch for the new superclassed device */
	g_signal_connect(plugin,
			 "device-added",
			 G_CALLBACK(fu_test_plugin_backend_proxy_device_added_cb),
			 &device_new);

	fu_device_set_specialized_gtype(device, FU_TYPE_DEVICE);
	fu_device_set_proxy_gtype(device, FU_TYPE_SELF_TEST_DEVICE);
	ret = fu_plugin_runner_backend_device_added(plugin, device, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* check device was constructed */
	g_assert_nonnull(device_new);
	g_assert_true(FU_IS_DEVICE(device_new));

	/* check proxy was constructed */
	proxy = fu_device_get_proxy(device_new, &error);
	g_assert_no_error(error);
	g_assert_nonnull(proxy);
	g_assert_true(FU_IS_SELF_TEST_DEVICE(proxy));
}

static void
fu_plugin_quirks_device_func(void)
{
	FuDevice *device_tmp;
	GPtrArray *children;
	gboolean ret;
	g_autofree gchar *testdatadir = NULL;
	g_autoptr(FuDevice) child1 = NULL;
	g_autoptr(FuDevice) child2 = NULL;
	g_autoptr(FuDevice) device = fu_device_new(NULL);
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(GError) error = NULL;

	/* set up test harness */
	testdatadir = g_test_build_filename(G_TEST_DIST, "tests", "quirks.d", NULL);
	fu_context_set_path(ctx, FU_PATH_KIND_DATADIR_QUIRKS, testdatadir);

	ret = fu_context_load_quirks(ctx, FU_QUIRKS_LOAD_FLAG_NO_CACHE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* use quirk file to set device attributes */
	fu_device_set_physical_id(device, "usb:00:05");
	fu_device_set_context(device, ctx);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_instance_id_full(device,
				       "USB\\VID_0BDA&PID_1100",
				       FU_DEVICE_INSTANCE_FLAG_GENERIC |
					   FU_DEVICE_INSTANCE_FLAG_QUIRKS);
	fu_device_add_instance_id(device, "USB\\VID_0BDA&PID_1100&CID_1234");
	g_assert_cmpstr(fu_device_get_name(device), ==, "Hub");

	/* ensure the non-customer-id instance ID is not available */
	g_assert_true(fu_device_has_instance_id(device,
						"USB\\VID_0BDA&PID_1100&CID_1234",
						FU_DEVICE_INSTANCE_FLAG_QUIRKS));
	g_assert_true(fu_device_has_instance_id(device,
						"USB\\VID_0BDA&PID_1100&CID_1234",
						FU_DEVICE_INSTANCE_FLAG_VISIBLE));
	g_assert_true(fu_device_has_instance_id(device,
						"USB\\VID_0BDA&PID_1100",
						FU_DEVICE_INSTANCE_FLAG_QUIRKS));
	g_assert_false(fu_device_has_instance_id(device,
						 "USB\\VID_0BDA&PID_1100",
						 FU_DEVICE_INSTANCE_FLAG_VISIBLE));

	/* ensure children are created */
	children = fu_device_get_children(device);
	g_assert_cmpint(children->len, ==, 1);
	device_tmp = g_ptr_array_index(children, 0);
	g_assert_cmpstr(fu_device_get_name(device_tmp), ==, "HDMI");
	g_assert_true(fu_device_has_flag(device_tmp, FWUPD_DEVICE_FLAG_UPDATABLE));

	/* get this one specific child */
	child1 = fu_device_get_child_by_logical_id(device, "USB\\VID_0763&PID_2806&I2C_01", &error);
	g_assert_no_error(error);
	g_assert_nonnull(child1);
	child2 = fu_device_get_child_by_logical_id(device, "SPI", &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_null(child2);
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/plugin", fu_plugin_func);
	g_test_add_func("/fwupd/plugin/vfuncs", fu_plugin_vfuncs_func);
	g_test_add_func("/fwupd/plugin/device-gtype", fu_plugin_device_gtype_func);
	g_test_add_func("/fwupd/plugin/backend-device", fu_plugin_backend_device_func);
	g_test_add_func("/fwupd/plugin/backend-proxy-device", fu_plugin_backend_proxy_device_func);
	g_test_add_func("/fwupd/plugin/config", fu_plugin_config_func);
	g_test_add_func("/fwupd/plugin/devices", fu_plugin_devices_func);
	g_test_add_func("/fwupd/plugin/delay", fu_plugin_delay_func);
	g_test_add_func("/fwupd/plugin/quirks-device", fu_plugin_quirks_device_func);
	g_test_add_func("/fwupd/archive/invalid", fu_archive_invalid_func);
	g_test_add_func("/fwupd/archive/cab", fu_archive_cab_func);
	return g_test_run();
}
