/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Mario Limonciello <mario.limonciello@dell.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupd.h>
#include <glib-object.h>
#include <glib/gstdio.h>
#include <stdlib.h>

#include "fu-plugin-private.h"

static void
_plugin_device_added_cb (FuPlugin *plugin, FuDevice *device, gpointer user_data)
{
	GPtrArray **devices = (GPtrArray **) user_data;
	g_ptr_array_add (*devices, device);
}

static void
fu_plugin_synapticsmst_func (void)
{
	gboolean ret;
	guint device_count;
	GPtrArray *devices = NULL;
	g_autoptr(GError) error = NULL;
	FuDevice *device = NULL;
	g_autoptr(FuPlugin) plugin = NULL;
	const gchar *test_directory;

	devices = g_ptr_array_new ();

	plugin = fu_plugin_new ();
	g_signal_connect (plugin, "device-added",
			  G_CALLBACK (_plugin_device_added_cb),
			  &devices);
	ret = fu_plugin_open (plugin, PLUGINBUILDDIR "/libfu_plugin_synapticsmst.so", &error);
	g_assert_no_error (error);
	g_assert (ret);
	ret = fu_plugin_runner_startup (plugin, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* Test with no Synaptics MST devices */
	test_directory = SOURCEDIR "/tests/no_devices";
	g_assert(g_file_test (test_directory, G_FILE_TEST_IS_DIR));

	g_setenv ("FWUPD_SYNAPTICSMST_FW_DIR", test_directory, TRUE);
	ret = fu_plugin_runner_coldplug (plugin, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* Emulate adding/removing a Dell TB16 dock */
	test_directory = SOURCEDIR "/tests/tb16_dock";

	g_assert (g_file_test (test_directory, G_FILE_TEST_IS_DIR));

	g_setenv ("FWUPD_SYNAPTICSMST_FW_DIR", test_directory, TRUE);
	ret = fu_plugin_runner_coldplug (plugin, &error);
	g_assert_no_error (error);
	g_assert (ret);

	device_count = devices->len;
	g_assert_cmpuint (device_count, ==, 2);

	for (guint i = 0; i < device_count; i++) {
		device = g_ptr_array_index (devices, i);
		g_assert_cmpstr (fu_device_get_version (device), ==, "3.10.002");
		g_ptr_array_remove (devices, device);
		fu_plugin_device_remove (plugin, device);
	}
	g_ptr_array_unref (devices);
}

int
main (int argc, char **argv)
{
	g_test_init (&argc, &argv, NULL);

	/* only critical and error are fatal */
	g_log_set_fatal_mask (NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);

	g_assert_cmpint (g_mkdir_with_parents ("/tmp/fwupd-self-test/var/lib/fwupd", 0755), ==, 0);

	/* tests go here */
	g_test_add_func ("/fwupd/plugin{synapticsmst}", fu_plugin_synapticsmst_func);
	return g_test_run ();
}
