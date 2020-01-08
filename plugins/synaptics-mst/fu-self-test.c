/*
 * Copyright (C) 2017 Mario Limonciello <mario.limonciello@dell.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupd.h>
#include <glib/gstdio.h>
#include <stdlib.h>

#include "fu-plugin-private.h"

static void
_plugin_device_added_cb (FuPlugin *plugin, FuDevice *device, gpointer user_data)
{
	GPtrArray **devices = (GPtrArray **) user_data;
	g_ptr_array_add (*devices, g_object_ref (device));
}

static void
_test_add_fake_devices_from_dir (FuPlugin *plugin, const gchar *path)
{
	const gchar *basename;
	gboolean ret;
	g_autoptr(FuQuirks) quirks = fu_quirks_new ();
	g_autoptr(GError) error = NULL;
	g_autoptr(GDir) dir = g_dir_open (path, 0, &error);
	g_assert_no_error (error);
	g_assert_nonnull (dir);
	while ((basename = g_dir_read_name (dir)) != NULL) {
		g_autofree gchar *fn = g_build_filename (path, basename, NULL);
		g_autoptr(FuUdevDevice) dev = NULL;
		if (!g_str_has_prefix (basename, "drm_dp_aux"))
			continue;
		dev = g_object_new (FU_TYPE_UDEV_DEVICE,
				    "quirks", quirks,
				    "physical-id", "PCI_SLOT_NAME=0000:3e:00.0",
				    "logical-id", basename,
				    "subsystem", "drm_dp_aux_dev",
				    "device-file", fn,
				    NULL);
		g_debug ("creating drm_dp_aux_dev object backed by %s", fn);
		ret = fu_plugin_runner_udev_device_added (plugin, dev, &error);
		g_assert_no_error (error);
		g_assert (ret);
	}
}

/* test with no Synaptics MST devices */
static void
fu_plugin_synaptics_mst_none_func (void)
{
	gboolean ret;
	g_autoptr(FuPlugin) plugin = fu_plugin_new ();
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) devices = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	g_autofree gchar *pluginfn = NULL;

	g_signal_connect (plugin, "device-added",
			  G_CALLBACK (_plugin_device_added_cb),
			  &devices);
	pluginfn = g_build_filename (PLUGINBUILDDIR,
				     "libfu_plugin_synaptics_mst." G_MODULE_SUFFIX,
				     NULL);
	ret = fu_plugin_open (plugin, pluginfn, &error);
	g_assert_no_error (error);
	g_assert (ret);
	ret = fu_plugin_runner_startup (plugin, &error);
	if (!ret && g_error_matches (error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED)) {
		g_test_skip ("Skipping tests due to unsupported configuration");
		return;
	}
	g_assert_no_error (error);
	g_assert (ret);

	_test_add_fake_devices_from_dir (plugin, SOURCEDIR "/tests/no_devices");
	g_assert_cmpint (devices->len, ==, 0);
}

/* emulate adding/removing a Dell TB16 dock */
static void
fu_plugin_synaptics_mst_tb16_func (void)
{
	gboolean ret;
	g_autoptr(FuPlugin) plugin = fu_plugin_new ();
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) devices = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	g_autofree gchar *pluginfn = NULL;

	g_signal_connect (plugin, "device-added",
			  G_CALLBACK (_plugin_device_added_cb),
			  &devices);
	pluginfn = g_build_filename (PLUGINBUILDDIR,
				     "libfu_plugin_synaptics_mst." G_MODULE_SUFFIX,
				     NULL);
	ret = fu_plugin_open (plugin, pluginfn, &error);
	g_assert_no_error (error);
	g_assert (ret);
	ret = fu_plugin_runner_startup (plugin, &error);
	if (!ret && g_error_matches (error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED)) {
		g_test_skip ("Skipping tests due to unsupported configuration");
		return;
	}
	g_assert_no_error (error);
	g_assert (ret);

	_test_add_fake_devices_from_dir (plugin, SOURCEDIR "/tests/tb16_dock");
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device = g_ptr_array_index (devices, i);
		g_autofree gchar *tmp = fu_device_to_string (device);
		g_debug ("%s", tmp);
	}
	g_assert_cmpint (devices->len, ==, 2);
}

int
main (int argc, char **argv)
{
	g_test_init (&argc, &argv, NULL);

	/* only critical and error are fatal */
	g_log_set_fatal_mask (NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);

	g_assert_cmpint (g_mkdir_with_parents ("/tmp/fwupd-self-test/var/lib/fwupd", 0755), ==, 0);

	/* tests go here */
	g_test_add_func ("/fwupd/plugin/synaptics_mst{none}", fu_plugin_synaptics_mst_none_func);
	g_test_add_func ("/fwupd/plugin/synaptics_mst{tb16}", fu_plugin_synaptics_mst_tb16_func);
	return g_test_run ();
}
