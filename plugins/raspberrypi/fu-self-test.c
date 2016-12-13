/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015-2016 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include <fwupd.h>
#include <glib-object.h>
#include <glib/gstdio.h>
#include <stdlib.h>

#include "fu-plugin-private.h"
#include "fu-plugin-raspberrypi.h"

static gchar *
fu_test_get_filename (const gchar *filename)
{
	gchar *tmp;
	char full_tmp[PATH_MAX];
	g_autofree gchar *path = NULL;
	path = g_build_filename (TESTDATADIR, filename, NULL);
	tmp = realpath (path, full_tmp);
	if (tmp == NULL)
		return NULL;
	return g_strdup (full_tmp);
}

static void
_plugin_status_changed_cb (FuPlugin *plugin, FwupdStatus status, gpointer user_data)
{
	guint *cnt = (guint *) user_data;
	(*cnt)++;
}

static void
_plugin_device_added_cb (FuPlugin *plugin, FuDevice *device, gpointer user_data)
{
	FuDevice **dev = (FuDevice **) user_data;
	*dev = g_object_ref (device);
}
static void
fu_plugin_raspberrypi_func (void)
{
	gboolean ret;
	guint cnt = 0;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *path = NULL;
	g_autofree gchar *pending_db = NULL;
	g_autofree gchar *fwfile = NULL;
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(GBytes) blob_fw = NULL;
	g_autoptr(GMappedFile) mapped_file = NULL;

	/* test location */
	path = fu_test_get_filename ("rpiboot");
	if (path == NULL) {
		g_test_skip ("no rpiboot available");
		return;
	}

	/* create a fake device */
	plugin = fu_plugin_new ();
	ret = fu_plugin_open (plugin, ".libs/libfu_plugin_raspberrypi.so", &error);
	g_assert_no_error (error);
	g_assert (ret);

	fu_plugin_raspberrypi_set_fw_dir (plugin, path);
	g_signal_connect (plugin, "device-added",
			  G_CALLBACK (_plugin_device_added_cb),
			  &device);
	g_signal_connect (plugin, "status-changed",
			  G_CALLBACK (_plugin_status_changed_cb),
			  &cnt);
	ret = fu_plugin_runner_startup (plugin, &error);
	g_assert_no_error (error);
	g_assert (ret);
	ret = fu_plugin_runner_coldplug (plugin, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* check we did the right thing */
	g_assert_cmpint (cnt, ==, 0);
	g_assert (device != NULL);
	g_assert_cmpstr (fu_device_get_id (device), ==, "raspberry-pi");
	g_assert_cmpstr (fu_device_get_guid_default (device), ==,
			 "91dd7368-8640-5d72-a217-a505c034dd0b");
	g_assert_cmpstr (fu_device_get_version (device), ==,
			 "20150803");

	/* ensure clean */
	g_unlink ("/tmp/rpiboot/start.elf");

	/* do update */
	fu_plugin_raspberrypi_set_fw_dir (plugin, "/tmp/rpiboot");
	fwfile = fu_test_get_filename ("rpiupdate/firmware.bin");
	g_assert (fwfile != NULL);
	mapped_file = g_mapped_file_new (fwfile, FALSE, &error);
	g_assert_no_error (error);
	g_assert (mapped_file != NULL);
	blob_fw = g_mapped_file_get_bytes (mapped_file);
	ret = fu_plugin_runner_update (plugin, device, NULL, blob_fw,
				       FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (cnt, ==, 3);

	/* check the file was exploded to the right place */
	g_assert (g_file_test ("/tmp/rpiboot/start.elf", G_FILE_TEST_EXISTS));
	g_assert (g_file_test ("/tmp/rpiboot/overlays/test.dtb", G_FILE_TEST_EXISTS));
	g_assert_cmpstr (fu_device_get_version (device), ==,
			 "20150805");

	/* clean up */
	pending_db = g_build_filename (LOCALSTATEDIR, "lib", "fwupd", "pending.db", NULL);
	g_unlink (pending_db);
}

int
main (int argc, char **argv)
{
	g_test_init (&argc, &argv, NULL);

	/* only critical and error are fatal */
	g_log_set_fatal_mask (NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);

	g_assert_cmpint (g_mkdir_with_parents ("/tmp/fwupd-self-test/var/lib/fwupd", 0755), ==, 0);

	/* tests go here */
	g_test_add_func ("/fwupd/plugin{raspberrypi}", fu_plugin_raspberrypi_func);
	return g_test_run ();
}
