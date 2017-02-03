/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
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
#include <gio/gfiledescriptorbased.h>
#include <stdlib.h>

#include "fu-keyring.h"
#include "fu-pending.h"
#include "fu-plugin-private.h"

static GMainLoop *_test_loop = NULL;
static guint _test_loop_timeout_id = 0;

static gboolean
fu_test_hang_check_cb (gpointer user_data)
{
	g_main_loop_quit (_test_loop);
	_test_loop_timeout_id = 0;
	return G_SOURCE_REMOVE;
}

static void
fu_test_loop_run_with_timeout (guint timeout_ms)
{
	g_assert (_test_loop_timeout_id == 0);
	g_assert (_test_loop == NULL);
	_test_loop = g_main_loop_new (NULL, FALSE);
	_test_loop_timeout_id = g_timeout_add (timeout_ms, fu_test_hang_check_cb, NULL);
	g_main_loop_run (_test_loop);
}

static void
fu_test_loop_quit (void)
{
	if (_test_loop_timeout_id > 0) {
		g_source_remove (_test_loop_timeout_id);
		_test_loop_timeout_id = 0;
	}
	if (_test_loop != NULL) {
		g_main_loop_quit (_test_loop);
		g_main_loop_unref (_test_loop);
		_test_loop = NULL;
	}
}

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
	fu_test_loop_quit ();
}

static void
_plugin_device_added_cb (FuPlugin *plugin, FuDevice *device, gpointer user_data)
{
	FuDevice **dev = (FuDevice **) user_data;
	*dev = g_object_ref (device);
	fu_test_loop_quit ();
}

static void
fu_plugin_delay_func (void)
{
	FuDevice *device_tmp;
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuDevice) device = NULL;

	plugin = fu_plugin_new ();
	g_signal_connect (plugin, "device-added",
			  G_CALLBACK (_plugin_device_added_cb),
			  &device_tmp);
	g_signal_connect (plugin, "device-removed",
			  G_CALLBACK (_plugin_device_added_cb),
			  &device_tmp);

	/* add device straight away */
	device = fu_device_new ();
	fu_device_set_id (device, "testdev");
	fu_plugin_device_add (plugin, device);
	g_assert (device_tmp != NULL);
	g_assert_cmpstr (fu_device_get_id (device_tmp), ==, "testdev");
	g_clear_object (&device_tmp);

	/* remove device */
	fu_plugin_device_remove (plugin, device);
	g_assert (device_tmp != NULL);
	g_assert_cmpstr (fu_device_get_id (device_tmp), ==, "testdev");
	g_clear_object (&device_tmp);

	/* add it with a small delay */
	fu_plugin_device_add_delay (plugin, device);
	g_assert (device_tmp == NULL);
	fu_test_loop_run_with_timeout (1000);
	g_assert (device_tmp != NULL);
	g_assert_cmpstr (fu_device_get_id (device_tmp), ==, "testdev");
	g_clear_object (&device_tmp);

	/* add it again, twice quickly */
	fu_plugin_device_add_delay (plugin, device);
	g_test_expect_message (G_LOG_DOMAIN,
			       G_LOG_LEVEL_WARNING,
			       "ignoring add-delay as device * already pending");
	fu_plugin_device_add_delay (plugin, device);
	g_test_assert_expected_messages ();
	g_assert (device_tmp == NULL);
	fu_test_loop_run_with_timeout (1000);
	g_assert (device_tmp != NULL);
	g_assert_cmpstr (fu_device_get_id (device_tmp), ==, "testdev");
	g_clear_object (&device_tmp);
}

static void
fu_plugin_module_func (void)
{
	GError *error = NULL;
	FuDevice *device_tmp;
	FwupdResult *res;
	gboolean ret;
	guint cnt = 0;
	g_autofree gchar *mapped_file_fn = NULL;
	g_autofree gchar *pending_cap = NULL;
	g_autofree gchar *pending_db = NULL;
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuPending) pending = NULL;
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(GBytes) blob_cab = NULL;
	g_autoptr(GMappedFile) mapped_file = NULL;

	g_setenv ("FWUPD_ENABLE_TEST_PLUGIN", "1", TRUE);

	/* create a fake device */
	plugin = fu_plugin_new ();
	ret = fu_plugin_open (plugin, "../plugins/test/.libs/libfu_plugin_test.so", &error);
	g_assert_no_error (error);
	g_assert (ret);
	ret = fu_plugin_runner_startup (plugin, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_signal_connect (plugin, "device-added",
			  G_CALLBACK (_plugin_device_added_cb),
			  &device);
	g_signal_connect (plugin, "status-changed",
			  G_CALLBACK (_plugin_status_changed_cb),
			  &cnt);
	ret = fu_plugin_runner_coldplug (plugin, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* check we did the right thing */
	g_assert_cmpint (cnt, ==, 0);
	g_assert (device != NULL);
	g_assert_cmpstr (fu_device_get_id (device), ==, "FakeDevice");
	g_assert_cmpstr (fu_device_get_guid_default (device), ==,
			 "00000000-0000-0000-0000-000000000000");
	g_assert_cmpstr (fu_device_get_name (device), ==,
			 "Integrated Webcam™");

	/* schedule an offline update */
	mapped_file_fn = fu_test_get_filename ("colorhug/firmware.bin");
	mapped_file = g_mapped_file_new (mapped_file_fn, FALSE, &error);
	g_assert_no_error (error);
	g_assert (mapped_file != NULL);
	blob_cab = g_mapped_file_get_bytes (mapped_file);
	ret = fu_plugin_runner_update (plugin, device, blob_cab, NULL,
				  FWUPD_INSTALL_FLAG_OFFLINE, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (cnt, ==, 1);

	/* lets check the pending */
	pending = fu_pending_new ();
	res = fu_pending_get_device (pending, fu_device_get_id (device), &error);
	g_assert_no_error (error);
	g_assert (res != NULL);
	g_assert_cmpint (fu_device_get_update_state (res), ==, FWUPD_UPDATE_STATE_PENDING);
	g_assert_cmpstr (fu_device_get_update_error (res), ==, NULL);
	g_assert_cmpstr (fu_device_get_update_filename (res), !=, NULL);

	/* save this; we'll need to delete it later */
	pending_cap = g_strdup (fu_device_get_update_filename (res));
	g_object_unref (res);

	/* lets do this online */
	ret = fu_plugin_runner_update (plugin, device, blob_cab, NULL,
				  FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (cnt, ==, 3);

	/* lets check the pending */
	res = fu_pending_get_device (pending, fu_device_get_id (device), &error);
	g_assert_no_error (error);
	g_assert (res != NULL);
	g_assert_cmpint (fu_device_get_update_state (res), ==, FWUPD_UPDATE_STATE_SUCCESS);
	g_assert_cmpstr (fu_device_get_update_error (res), ==, NULL);
	g_object_unref (res);

	/* get the status */
	device_tmp = fu_device_new ();
	fu_device_set_id (device_tmp, "FakeDevice");
	ret = fu_plugin_runner_get_results (plugin, device_tmp, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (fu_device_get_update_state (device_tmp), ==, FWUPD_UPDATE_STATE_SUCCESS);
	g_assert_cmpstr (fu_device_get_update_error (device_tmp), ==, NULL);

	/* clear */
	ret = fu_plugin_runner_clear_results (plugin, device_tmp, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* re-get the status */
	ret = fu_plugin_runner_get_results (plugin, device_tmp, &error);
	g_assert_error (error, FWUPD_ERROR, FWUPD_ERROR_NOTHING_TO_DO);
	g_assert (!ret);

	g_object_unref (device_tmp);
	g_clear_error (&error);

	/* delete files */
	pending_db = g_build_filename (LOCALSTATEDIR, "lib", "fwupd", "pending.db", NULL);
	g_unlink (pending_db);
	g_unlink (pending_cap);
}

static void
fu_pending_func (void)
{
	GError *error = NULL;
	gboolean ret;
	FwupdResult *res;
	g_autoptr(FuPending) pending = NULL;
	g_autofree gchar *dirname = NULL;
	g_autofree gchar *filename = NULL;

	/* create */
	pending = fu_pending_new ();
	g_assert (pending != NULL);

	/* delete the database */
	dirname = g_build_filename (LOCALSTATEDIR, "lib", "fwupd", NULL);
	if (!g_file_test (dirname, G_FILE_TEST_IS_DIR))
		return;
	filename = g_build_filename (dirname, "pending.db", NULL);
	g_unlink (filename);

	/* add a device */
	res = FWUPD_RESULT (fu_device_new ());
	fu_device_set_id (res, "self-test");
	fu_device_set_update_filename (res, "/var/lib/dave.cap"),
	fu_device_set_name (FU_DEVICE (res), "ColorHug"),
	fu_device_set_version (res, "3.0.1"),
	fu_device_set_update_version (res, "3.0.2");
	ret = fu_pending_add_device (pending, res, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_object_unref (res);

	/* ensure database was created */
	g_assert (g_file_test (filename, G_FILE_TEST_EXISTS));

	/* add some extra data */
	res = fwupd_result_new ();
	fu_device_set_id (res, "self-test");
	ret = fu_pending_set_state (pending, res, FWUPD_UPDATE_STATE_PENDING, &error);
	g_assert_no_error (error);
	g_assert (ret);
	ret = fu_pending_set_error_msg (pending, res, "word", &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_object_unref (res);

	/* get device */
	res = fu_pending_get_device (pending, "self-test", &error);
	g_assert_no_error (error);
	g_assert (res != NULL);
	g_assert_cmpstr (fwupd_result_get_device_id (res), ==, "self-test");
	g_assert_cmpstr (fwupd_result_get_update_filename (res), ==, "/var/lib/dave.cap");
	g_assert_cmpstr (fwupd_result_get_device_name (res), ==, "ColorHug");
	g_assert_cmpstr (fwupd_result_get_device_version (res), ==, "3.0.1");
	g_assert_cmpstr (fwupd_result_get_update_version (res), ==, "3.0.2");
	g_assert_cmpint (fwupd_result_get_update_state (res), ==, FWUPD_UPDATE_STATE_PENDING);
	g_assert_cmpstr (fwupd_result_get_update_error (res), ==, "word");
	g_object_unref (res);

	/* get device that does not exist */
	res = fu_pending_get_device (pending, "XXXXXXXXXXXXX", &error);
	g_assert_error (error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert (res == NULL);
	g_clear_error (&error);

	/* remove device */
	res = fwupd_result_new ();
	fu_device_set_id (res, "self-test");
	ret = fu_pending_remove_device (pending, res, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_object_unref (res);

	/* get device that does not exist */
	res = fu_pending_get_device (pending, "self-test", &error);
	g_assert_error (error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert (res == NULL);
	g_clear_error (&error);
}

static void
fu_keyring_func (void)
{
	gboolean ret;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *fw_fail = NULL;
	g_autofree gchar *fw_pass = NULL;
	g_autofree gchar *pki_dir = NULL;
	g_autoptr(FuKeyring) keyring = NULL;
	const gchar *sig =
	"iQEcBAABCAAGBQJVt0B4AAoJEEim2A5FOLrCFb8IAK+QTLY34Wu8xZ8nl6p3JdMu"
	"HOaifXAmX7291UrsFRwdabU2m65pqxQLwcoFrqGv738KuaKtu4oIwo9LIrmmTbEh"
	"IID8uszxBt0bMdcIHrvwd+ADx+MqL4hR3guXEE3YOBTLvv2RF1UBcJPInNf/7Ui1"
	"3lW1c3trL8RAJyx1B5RdKqAMlyfwiuvKM5oT4SN4uRSbQf+9mt78ZSWfJVZZH/RR"
	"H9q7PzR5GdmbsRPM0DgC27Trvqjo3MzoVtoLjIyEb/aWqyulUbnJUNKPYTnZgkzM"
	"v2yVofWKIM3e3wX5+MOtf6EV58mWa2cHJQ4MCYmpKxbIvAIZagZ4c9A8BA6tQWg="
	"=fkit";

	/* add test keys to keyring */
	keyring = fu_keyring_new ();
	pki_dir = fu_test_get_filename ("pki");
	ret = fu_keyring_add_public_keys (keyring, pki_dir, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* verify */
	fw_pass = fu_test_get_filename ("colorhug/firmware.bin");
	ret = fu_keyring_verify_file (keyring, fw_pass, sig, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* verify will fail */
	fw_fail = fu_test_get_filename ("colorhug/colorhug-als-3.0.2.cab");
	ret = fu_keyring_verify_file (keyring, fw_fail, sig, &error);
	g_assert_error (error, FWUPD_ERROR, FWUPD_ERROR_SIGNATURE_INVALID);
	g_assert (!ret);
	g_clear_error (&error);
}

int
main (int argc, char **argv)
{
	g_test_init (&argc, &argv, NULL);

	/* only critical and error are fatal */
	g_log_set_fatal_mask (NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);

	g_assert_cmpint (g_mkdir_with_parents ("/tmp/fwupd-self-test/var/lib/fwupd", 0755), ==, 0);

	/* tests go here */
	g_test_add_func ("/fwupd/pending", fu_pending_func);
	g_test_add_func ("/fwupd/plugin{delay}", fu_plugin_delay_func);
	g_test_add_func ("/fwupd/plugin{module}", fu_plugin_module_func);
	g_test_add_func ("/fwupd/keyring", fu_keyring_func);
	return g_test_run ();
}
