/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015-2017 Richard Hughes <richard@hughsie.com>
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

#include <appstream-glib.h>
#include <fwupd.h>
#include <glib-object.h>
#include <glib/gstdio.h>
#include <gio/gfiledescriptorbased.h>
#include <stdlib.h>
#include <string.h>

#include "fu-keyring.h"
#include "fu-pending.h"
#include "fu-plugin-private.h"
#include "fu-hwids.h"
#include "fu-test.h"

#ifdef ENABLE_GPG
#include "fu-keyring-gpg.h"
#endif
#ifdef ENABLE_PKCS7
#include "fu-keyring-pkcs7.h"
#endif

static void
fu_hwids_func (void)
{
	g_autoptr(FuHwids) hwids = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *sysfsdir = NULL;
	g_autofree gchar *testdir = NULL;
	gboolean ret;

	struct {
		const gchar *key;
		const gchar *value;
	} guids[] = {
		{ "Manufacturer",	"11b4a036-3b64-5421-a372-22c07df10a4d" },
		{ "HardwareID-14",	"11b4a036-3b64-5421-a372-22c07df10a4d" },
		{ "HardwareID-13",	"7ccbb6f1-9641-5f84-b00d-51ff218a4066" },
		{ "HardwareID-12",	"482f3f58-6045-593a-9be4-611717ce4770" },
		{ "HardwareID-11",	"6525c6e5-28e9-5f9c-abe4-20fd82504002" },
		{ "HardwareID-10",	"c00fe015-014c-5301-90d1-b5c8ab037eb4" },
		{ "HardwareID-9",	"6525c6e5-28e9-5f9c-abe4-20fd82504002" },
		{ "HardwareID-8",	"c00fe015-014c-5301-90d1-b5c8ab037eb4" },
		{ "HardwareID-7",	"5a127cba-be28-5d3b-84f0-0e450d266d97" },
		{ "HardwareID-6",	"2c2d02cc-357e-539d-a44d-d10e902391dd" },
		{ "HardwareID-5",	"7ccbb6f1-9641-5f84-b00d-51ff218a4066" },
		{ "HardwareID-4",	"d78b474d-dee0-5412-bc9d-e9f7d7783df2" },
		{ "HardwareID-3",	"a2f225b3-f4f0-5590-8973-08dd81602d69" },
		{ "HardwareID-2",	"2e7c87e3-a52c-537f-a5f6-907110143cf7" },
		{ "HardwareID-1",	"6453b900-1fd8-55fb-a936-7fca22823bcc" },
		{ "HardwareID-0",	"d777e0a5-4db6-51b4-a927-86d4ccdc5c0d" },
		{ NULL, NULL }
	};

	sysfsdir = fu_test_get_filename (TESTDATADIR, "hwids");
	g_assert (sysfsdir != NULL);

	hwids = fu_hwids_new ();
	ret = fu_hwids_setup (hwids, sysfsdir, &error);
	g_assert_no_error (error);
	g_assert (ret);

	g_assert_cmpstr (fu_hwids_get_value (hwids, FU_HWIDS_KEY_MANUFACTURER), ==,
			 "To be filled by O.E.M.");
	g_assert_cmpstr (fu_hwids_get_value (hwids, FU_HWIDS_KEY_ENCLOSURE_KIND), ==,
			 "3");
	g_assert_cmpstr (fu_hwids_get_value (hwids, FU_HWIDS_KEY_FAMILY), ==,
			 "To be filled by O.E.M.");
	g_assert_cmpstr (fu_hwids_get_value (hwids, FU_HWIDS_KEY_PRODUCT_NAME), ==,
			 "To be filled by O.E.M.");
	g_assert_cmpstr (fu_hwids_get_value (hwids, FU_HWIDS_KEY_BIOS_VENDOR), ==,
			 "American Megatrends Inc.");
	g_assert_cmpstr (fu_hwids_get_value (hwids, FU_HWIDS_KEY_BIOS_VERSION), ==, "1201");
	g_assert_cmpstr (fu_hwids_get_value (hwids, FU_HWIDS_KEY_BIOS_MAJOR_RELEASE), ==, "4");
	g_assert_cmpstr (fu_hwids_get_value (hwids, FU_HWIDS_KEY_BIOS_MINOR_RELEASE), ==, "6");
	g_assert_cmpstr (fu_hwids_get_value (hwids, FU_HWIDS_KEY_PRODUCT_SKU), ==, "SKU");
	for (guint i = 0; guids[i].key != NULL; i++) {
		g_autofree gchar *guid = fu_hwids_get_guid (hwids, guids[i].key, &error);
		g_assert_no_error (error);
		g_assert_cmpstr (guid, ==, guids[i].value);
	}
	for (guint i = 0; guids[i].key != NULL; i++)
		g_assert (fu_hwids_has_guid (hwids, guids[i].value));
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
	fu_plugin_device_add_delay (plugin, device);
	g_assert (device_tmp == NULL);
	fu_test_loop_run_with_timeout (1000);
	g_assert (device_tmp != NULL);
	g_assert_cmpstr (fu_device_get_id (device_tmp), ==, "testdev");
	g_clear_object (&device_tmp);
}

static void
_plugin_device_register_cb (FuPlugin *plugin, FuDevice *device, gpointer user_data)
{
	/* fake being a daemon */
	fu_plugin_runner_device_register (plugin, device);
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

	/* the test plugin is only usable if this is set */
	g_setenv ("FWUPD_TESTS", "true", TRUE);

	/* create a fake device */
	plugin = fu_plugin_new ();
	ret = fu_plugin_open (plugin, PLUGINBUILDDIR "/libfu_plugin_test.so", &error);
	g_assert_no_error (error);
	g_assert (ret);
	ret = fu_plugin_runner_startup (plugin, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_signal_connect (plugin, "device-added",
			  G_CALLBACK (_plugin_device_added_cb),
			  &device);
	g_signal_connect (plugin, "device-register",
			  G_CALLBACK (_plugin_device_register_cb),
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
	g_assert_cmpstr (fu_device_get_version_lowest (device), ==, "1.2.0");
	g_assert_cmpstr (fu_device_get_version (device), ==, "1.2.3");
	g_assert_cmpstr (fu_device_get_version_bootloader (device), ==, "0.1.2");
	g_assert_cmpstr (fu_device_get_guid_default (device), ==,
			 "b585990a-003e-5270-89d5-3705a17f9a43");
	g_assert_cmpstr (fu_device_get_name (device), ==,
			 "Integrated Webcam™");

	/* schedule an offline update */
	mapped_file_fn = fu_test_get_filename (TESTDATADIR, "colorhug/firmware.bin");
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
	g_assert_cmpint (cnt, ==, 4);

	/* check the new version */
	g_assert_cmpstr (fu_device_get_version (device), ==, "1.2.4");
	g_assert_cmpstr (fu_device_get_version_bootloader (device), ==, "0.1.2");

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
	FwupdDevice *dev;
	FwupdRelease *rel;
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
	dev = fwupd_result_get_device (res);
	g_assert_cmpstr (fwupd_device_get_id (dev), ==, "self-test");
	g_assert_cmpstr (fwupd_device_get_name (dev), ==, "ColorHug");
	g_assert_cmpstr (fwupd_device_get_version (dev), ==, "3.0.1");
	g_assert_cmpint (fwupd_result_get_update_state (res), ==, FWUPD_UPDATE_STATE_PENDING);
	g_assert_cmpstr (fwupd_result_get_update_error (res), ==, "word");
	rel = fwupd_result_get_release (res);
	g_assert (rel != NULL);
	g_assert_cmpstr (fwupd_release_get_filename (rel), ==, "/var/lib/dave.cap");
	g_assert_cmpstr (fwupd_release_get_version (rel), ==, "3.0.2");
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
fu_keyring_gpg_func (void)
{
#ifdef ENABLE_GPG
	gboolean ret;
	g_autofree gchar *fw_fail = NULL;
	g_autofree gchar *fw_pass = NULL;
	g_autofree gchar *pki_dir = NULL;
	g_autoptr(FuKeyring) keyring = NULL;
	g_autoptr(FuKeyringResult) result_fail = NULL;
	g_autoptr(FuKeyringResult) result_pass = NULL;
	g_autoptr(GBytes) blob_fail = NULL;
	g_autoptr(GBytes) blob_pass = NULL;
	g_autoptr(GBytes) blob_sig = NULL;
	g_autoptr(GError) error = NULL;
	const gchar *sig_gpgme =
	"-----BEGIN PGP SIGNATURE-----\n"
	"Version: GnuPG v1\n\n"
	"iQEcBAABCAAGBQJVt0B4AAoJEEim2A5FOLrCFb8IAK+QTLY34Wu8xZ8nl6p3JdMu"
	"HOaifXAmX7291UrsFRwdabU2m65pqxQLwcoFrqGv738KuaKtu4oIwo9LIrmmTbEh"
	"IID8uszxBt0bMdcIHrvwd+ADx+MqL4hR3guXEE3YOBTLvv2RF1UBcJPInNf/7Ui1"
	"3lW1c3trL8RAJyx1B5RdKqAMlyfwiuvKM5oT4SN4uRSbQf+9mt78ZSWfJVZZH/RR"
	"H9q7PzR5GdmbsRPM0DgC27Trvqjo3MzoVtoLjIyEb/aWqyulUbnJUNKPYTnZgkzM"
	"v2yVofWKIM3e3wX5+MOtf6EV58mWa2cHJQ4MCYmpKxbIvAIZagZ4c9A8BA6tQWg="
	"=fkit\n"
	"-----END PGP SIGNATURE-----\n";

	/* add keys to keyring */
	keyring = fu_keyring_gpg_new ();
	ret = fu_keyring_setup (keyring, &error);
	g_assert_no_error (error);
	g_assert_true (ret);
	pki_dir = fu_test_get_filename (TESTDATADIR, "pki");
	g_assert_nonnull (pki_dir);
	ret = fu_keyring_add_public_keys (keyring, pki_dir, &error);
	g_assert_no_error (error);
	g_assert_true (ret);

	/* verify with GnuPG */
	fw_pass = fu_test_get_filename (TESTDATADIR, "colorhug/firmware.bin");
	g_assert_nonnull (fw_pass);
	blob_pass = fu_common_get_contents_bytes (fw_pass, &error);
	g_assert_no_error (error);
	g_assert_nonnull (blob_pass);
	blob_sig = g_bytes_new_static (sig_gpgme, strlen (sig_gpgme));
	result_pass = fu_keyring_verify_data (keyring, blob_pass, blob_sig, &error);
	g_assert_no_error (error);
	g_assert_nonnull (result_pass);
	g_assert_cmpint (fu_keyring_result_get_timestamp (result_pass), == , 1438072952);
	g_assert_cmpstr (fu_keyring_result_get_authority (result_pass), == ,
			 "3FC6B804410ED0840D8F2F9748A6D80E4538BAC2");

	/* verify will fail with GnuPG */
	fw_fail = fu_test_get_filename (TESTDATADIR, "colorhug/colorhug-als-3.0.2.cab");
	g_assert_nonnull (fw_fail);
	blob_fail = fu_common_get_contents_bytes (fw_fail, &error);
	g_assert_no_error (error);
	g_assert_nonnull (blob_fail);
	result_fail = fu_keyring_verify_data (keyring, blob_fail, blob_sig, &error);
	g_assert_error (error, FWUPD_ERROR, FWUPD_ERROR_SIGNATURE_INVALID);
	g_assert_null (result_fail);
	g_clear_error (&error);
#else
	g_test_skip ("no GnuPG support enabled");
#endif
}

static void
fu_keyring_pkcs7_func (void)
{
#ifdef ENABLE_PKCS7
	gboolean ret;
	g_autofree gchar *fw_fail = NULL;
	g_autofree gchar *fw_pass = NULL;
	g_autofree gchar *pki_dir = NULL;
	g_autofree gchar *sig_fn = NULL;
	g_autofree gchar *sig_fn2 = NULL;
	g_autoptr(FuKeyring) keyring = NULL;
	g_autoptr(FuKeyringResult) result_fail = NULL;
	g_autoptr(FuKeyringResult) result_pass = NULL;
	g_autoptr(GBytes) blob_fail = NULL;
	g_autoptr(GBytes) blob_pass = NULL;
	g_autoptr(GBytes) blob_sig = NULL;
	g_autoptr(GBytes) blob_sig2 = NULL;
	g_autoptr(GError) error = NULL;

	/* add keys to keyring */
	keyring = fu_keyring_pkcs7_new ();
	ret = fu_keyring_setup (keyring, &error);
	g_assert_no_error (error);
	g_assert_true (ret);
	pki_dir = fu_test_get_filename (TESTDATADIR_SRC, "pki");
	g_assert_nonnull (pki_dir);
	ret = fu_keyring_add_public_keys (keyring, pki_dir, &error);
	g_assert_no_error (error);
	g_assert_true (ret);

	/* verify with a signature from the old LVFS */
	fw_pass = fu_test_get_filename (TESTDATADIR_SRC, "colorhug/firmware.bin");
	g_assert_nonnull (fw_pass);
	blob_pass = fu_common_get_contents_bytes (fw_pass, &error);
	g_assert_no_error (error);
	g_assert_nonnull (blob_pass);
	sig_fn = fu_test_get_filename (TESTDATADIR_SRC, "colorhug/firmware.bin.p7b");
	g_assert_nonnull (sig_fn);
	blob_sig = fu_common_get_contents_bytes (sig_fn, &error);
	g_assert_no_error (error);
	g_assert_nonnull (blob_sig);
	result_pass = fu_keyring_verify_data (keyring, blob_pass, blob_sig, &error);
	g_assert_no_error (error);
	g_assert_nonnull (result_pass);
	g_assert_cmpint (fu_keyring_result_get_timestamp (result_pass), >= , 1502871248);
	g_assert_cmpstr (fu_keyring_result_get_authority (result_pass), == , "O=Linux Vendor Firmware Project,CN=LVFS CA");

	/* verify will fail with a self-signed signature */
	sig_fn2 = fu_test_get_filename (TESTDATADIR_DST, "colorhug/firmware.bin.p7c");
	g_assert_nonnull (sig_fn2);
	blob_sig2 = fu_common_get_contents_bytes (sig_fn2, &error);
	g_assert_no_error (error);
	g_assert_nonnull (blob_sig2);
	result_fail = fu_keyring_verify_data (keyring, blob_pass, blob_sig2, &error);
	g_assert_error (error, FWUPD_ERROR, FWUPD_ERROR_SIGNATURE_INVALID);
	g_assert_null (result_fail);
	g_clear_error (&error);

	/* verify will fail with valid signature and different data */
	fw_fail = fu_test_get_filename (TESTDATADIR, "colorhug/colorhug-als-3.0.2.cab");
	g_assert_nonnull (fw_fail);
	blob_fail = fu_common_get_contents_bytes (fw_fail, &error);
	g_assert_no_error (error);
	g_assert_nonnull (blob_fail);
	result_fail = fu_keyring_verify_data (keyring, blob_fail, blob_sig, &error);
	g_assert_error (error, FWUPD_ERROR, FWUPD_ERROR_SIGNATURE_INVALID);
	g_assert_null (result_fail);
	g_clear_error (&error);
#else
	g_test_skip ("no GnuTLS support enabled");
#endif
}

static void
fu_common_firmware_builder_func (void)
{
	const gchar *data;
	g_autofree gchar *archive_fn = NULL;
	g_autofree gchar *bwrap_fn = NULL;
	g_autoptr(GBytes) archive_blob = NULL;
	g_autoptr(GBytes) firmware_blob = NULL;
	g_autoptr(GError) error = NULL;

	/* we can't do this in travis: capset failed: Operation not permitted */
	bwrap_fn = g_find_program_in_path ("bwrap");
	if (bwrap_fn == NULL) {
		g_test_skip ("no bwrap in path, so skipping");
		return;
	}

	/* get test file */
	archive_fn = fu_test_get_filename (TESTDATADIR, "builder/firmware.tar");
	g_assert (archive_fn != NULL);
	archive_blob = fu_common_get_contents_bytes (archive_fn, &error);
	g_assert_no_error (error);
	g_assert (archive_blob != NULL);

	/* generate the firmware */
	firmware_blob = fu_common_firmware_builder (archive_blob,
						    "startup.sh",
						    "firmware.bin",
						    &error);
	g_assert_no_error (error);
	g_assert (firmware_blob != NULL);

	/* check it */
	data = g_bytes_get_data (firmware_blob, NULL);
	g_assert_cmpstr (data, ==, "xobdnas eht ni gninnur");
}

static void
fu_test_stdout_cb (const gchar *line, gpointer user_data)
{
	guint *lines = (guint *) user_data;
	g_debug ("got '%s'", line);
	(*lines)++;
}

static void
fu_common_spawn_func (void)
{
	gboolean ret;
	guint lines = 0;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *fn = NULL;
	gchar *argv[3] = { "replace", "test", NULL };

	fn = fu_test_get_filename (TESTDATADIR, "spawn.sh");
	g_assert (fn != NULL);
	argv[0] = fn;
	ret = fu_common_spawn_sync ((const gchar * const *) argv,
				    fu_test_stdout_cb, &lines, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (lines, ==, 6);
}

int
main (int argc, char **argv)
{
	g_test_init (&argc, &argv, NULL);

	/* only critical and error are fatal */
	g_log_set_fatal_mask (NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);
	g_setenv ("G_MESSAGES_DEBUG", "all", TRUE);

	g_assert_cmpint (g_mkdir_with_parents ("/tmp/fwupd-self-test/var/lib/fwupd", 0755), ==, 0);

	/* tests go here */
	g_test_add_func ("/fwupd/hwids", fu_hwids_func);
	g_test_add_func ("/fwupd/pending", fu_pending_func);
	g_test_add_func ("/fwupd/plugin{delay}", fu_plugin_delay_func);
	g_test_add_func ("/fwupd/plugin{module}", fu_plugin_module_func);
	g_test_add_func ("/fwupd/keyring{gpg}", fu_keyring_gpg_func);
	g_test_add_func ("/fwupd/keyring{pkcs7}", fu_keyring_pkcs7_func);
	g_test_add_func ("/fwupd/common{spawn)", fu_common_spawn_func);
	g_test_add_func ("/fwupd/common{firmware-builder}", fu_common_firmware_builder_func);
	return g_test_run ();
}
