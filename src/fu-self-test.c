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

#include "fu-config.h"
#include "fu-device-private.h"
#include "fu-engine.h"
#include "fu-quirks.h"
#include "fu-keyring.h"
#include "fu-pending.h"
#include "fu-plugin-private.h"
#include "fu-progressbar.h"
#include "fu-hwids.h"
#include "fu-smbios.h"
#include "fu-test.h"

#ifdef ENABLE_GPG
#include "fu-keyring-gpg.h"
#endif
#ifdef ENABLE_PKCS7
#include "fu-keyring-pkcs7.h"
#endif

static void
fu_engine_require_hwid_func (void)
{
	const gchar *device_id = "test_device";
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autoptr(AsStore) store = NULL;
	g_autoptr(FuDevice) device = fu_device_new ();
	g_autoptr(FuEngine) engine = fu_engine_new ();
	g_autoptr(FuPlugin) plugin = fu_plugin_new ();
	g_autoptr(GBytes) blob_cab = NULL;
	g_autoptr(GError) error = NULL;

#if !AS_CHECK_VERSION(0,7,4)
	g_test_skip ("HWID requirements only supported with appstream-glib 0.7.4");
	return;
#endif

	/* get generated file as a blob */
	filename = fu_test_get_filename (TESTDATADIR, "missing-hwid/hwid-1.2.3.cab");
	g_assert (filename != NULL);
	blob_cab = fu_common_get_contents_bytes	(filename, &error);
	g_assert_no_error (error);
	g_assert (blob_cab != NULL);
	store = fu_engine_get_store_from_blob (engine, blob_cab, &error);
	g_assert_no_error (error);
	g_assert (store != NULL);

	/* add a dummy device */
	fu_device_set_id (device, "test_device");
	fu_device_add_guid (device, "12345678-1234-1234-1234-123456789012");
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_engine_add_device (engine, plugin, device);

	/* install it */
	ret = fu_engine_install (engine, device_id, store, blob_cab, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_error (error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_assert (error != NULL);
	g_assert_cmpstr (error->message, ==,
			 "no HWIDs matched 9342d47a-1bab-5709-9869-c840b2eac501");
	g_assert (!ret);
}

static void
fu_engine_func (void)
{
	FwupdRelease *rel;
	gboolean ret;
	g_autofree gchar *testdatadir = NULL;
	g_autoptr(FuDevice) device = fu_device_new ();
	g_autoptr(FuEngine) engine = fu_engine_new ();
	g_autoptr(FuPlugin) plugin = fu_plugin_new ();
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(GPtrArray) devices_pre = NULL;
	g_autoptr(GPtrArray) releases_dg = NULL;
	g_autoptr(GPtrArray) releases = NULL;
	g_autoptr(GPtrArray) releases_up = NULL;
	g_autoptr(GPtrArray) remotes = NULL;

	/* write a broken file */
	ret = g_file_set_contents ("/tmp/fwupd-self-test/broken.xml.gz",
				   "this is not a valid", -1, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* write the main file */
	ret = g_file_set_contents ("/tmp/fwupd-self-test/stable.xml",
				   "<components>"
				   "  <component type=\"firmware\">"
				   "    <id>test</id>"
				   "    <name>Test Device</name>"
				   "    <provides>"
				   "      <firmware type=\"flashed\">aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee</firmware>"
				   "    </provides>"
				   "    <releases>"
				   "      <release version=\"1.2.3\" date=\"2017-09-15\">"
				   "        <size type=\"installed\">123</size>"
				   "        <size type=\"download\">456</size>"
				   "        <location>https://test.org/foo.cab</location>"
				   "        <checksum filename=\"foo.cab\" target=\"container\" type=\"md5\">deadbeefdeadbeefdeadbeefdeadbeef</checksum>"
				   "        <checksum filename=\"firmware.bin\" target=\"content\" type=\"md5\">deadbeefdeadbeefdeadbeefdeadbeef</checksum>"
				   "      </release>"
				   "      <release version=\"1.2.2\" date=\"2017-09-01\">"
				   "        <size type=\"installed\">123</size>"
				   "        <size type=\"download\">456</size>"
				   "        <location>https://test.org/foo.cab</location>"
				   "        <checksum filename=\"foo.cab\" target=\"container\" type=\"md5\">deadbeefdeadbeefdeadbeefdeadbeef</checksum>"
				   "        <checksum filename=\"firmware.bin\" target=\"content\" type=\"md5\">deadbeefdeadbeefdeadbeefdeadbeef</checksum>"
				   "      </release>"
				   "    </releases>"
				   "  </component>"
				   "</components>", -1, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* write the extra file */
	ret = g_file_set_contents ("/tmp/fwupd-self-test/testing.xml",
				   "<components>"
				   "  <component type=\"firmware\">"
				   "    <id>test</id>"
				   "    <name>Test Device</name>"
				   "    <provides>"
				   "      <firmware type=\"flashed\">aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee</firmware>"
				   "    </provides>"
				   "    <releases>"
				   "      <release version=\"1.2.4\" date=\"2017-09-15\">"
				   "        <size type=\"installed\">123</size>"
				   "        <size type=\"download\">456</size>"
				   "        <location>https://test.org/foo.cab</location>"
				   "        <checksum filename=\"foo.cab\" target=\"container\" type=\"md5\">deadbeefdeadbeefdeadbeefdeadbeef</checksum>"
				   "        <checksum filename=\"firmware.bin\" target=\"content\" type=\"md5\">deadbeefdeadbeefdeadbeefdeadbeef</checksum>"
				   "      </release>"
				   "    </releases>"
				   "  </component>"
				   "</components>", -1, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* expect just one broken remote to fail */
	g_test_expect_message (G_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
			       "failed to load remote broken: *");

	testdatadir = fu_test_get_filename (TESTDATADIR, ".");
	g_assert (testdatadir != NULL);
	g_setenv ("FU_SELF_TEST_REMOTES_DIR", testdatadir, TRUE);
	ret = fu_engine_load (engine, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (fu_engine_get_status (engine), ==, FWUPD_STATUS_IDLE);
	g_test_assert_expected_messages ();

	/* return all the remotes, even the broken one */
	remotes = fu_engine_get_remotes (engine, &error);
	g_assert_no_error (error);
	g_assert (remotes != NULL);
	g_assert_cmpint (remotes->len, ==, 3);

	/* ensure there are no devices already */
	devices_pre = fu_engine_get_devices (engine, &error);
	g_assert_error (error, FWUPD_ERROR, FWUPD_ERROR_NOTHING_TO_DO);
	g_assert (devices_pre == NULL);
	g_clear_error (&error);

	/* add a device so we can get upgrades and downgrades */
	fu_device_set_version (device, "1.2.3");
	fu_device_set_id (device, "test_device");
	fu_device_set_name (device, "Test Device");
	fu_device_add_guid (device, "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee");
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_engine_add_device (engine, plugin, device);
	devices = fu_engine_get_devices (engine, &error);
	g_assert_no_error (error);
	g_assert (devices != NULL);
	g_assert_cmpint (devices->len, ==, 1);
	g_assert (fu_device_has_flag (device, FWUPD_DEVICE_FLAG_SUPPORTED));
	g_assert (fu_device_has_flag (device, FWUPD_DEVICE_FLAG_REGISTERED));

	/* get the releases for one device */
	releases = fu_engine_get_releases (engine, fu_device_get_id (device), &error);
	g_assert_no_error (error);
	g_assert (releases != NULL);
	g_assert_cmpint (releases->len, ==, 3);

	/* upgrades */
	releases_up = fu_engine_get_upgrades (engine, fu_device_get_id (device), &error);
	g_assert_no_error (error);
	g_assert (releases_up != NULL);
	g_assert_cmpint (releases_up->len, ==, 1);
	rel = FWUPD_RELEASE (g_ptr_array_index (releases_up, 0));
	g_assert_cmpstr (fwupd_release_get_version (rel), ==, "1.2.4");

	/* downgrades */
	releases_dg = fu_engine_get_downgrades (engine, fu_device_get_id (device), &error);
	g_assert_no_error (error);
	g_assert (releases_dg != NULL);
	g_assert_cmpint (releases_dg->len, ==, 1);
	rel = FWUPD_RELEASE (g_ptr_array_index (releases_dg, 0));
	g_assert_cmpstr (fwupd_release_get_version (rel), ==, "1.2.2");
}

static void
fu_device_metadata_func (void)
{
	g_autoptr(FuDevice) device = fu_device_new ();

	/* string */
	fu_device_set_metadata (device, "foo", "bar");
	g_assert_cmpstr (fu_device_get_metadata (device, "foo"), ==, "bar");
	fu_device_set_metadata (device, "foo", "baz");
	g_assert_cmpstr (fu_device_get_metadata (device, "foo"), ==, "baz");
	g_assert_null (fu_device_get_metadata (device, "unknown"));

	/* boolean */
	fu_device_set_metadata_boolean (device, "baz", TRUE);
	g_assert_cmpstr (fu_device_get_metadata (device, "baz"), ==, "true");
	g_assert_true (fu_device_get_metadata_boolean (device, "baz"));
	g_assert_false (fu_device_get_metadata_boolean (device, "unknown"));

	/* integer */
	fu_device_set_metadata_integer (device, "dum", 12345);
	g_assert_cmpstr (fu_device_get_metadata (device, "dum"), ==, "12345");
	g_assert_cmpint (fu_device_get_metadata_integer (device, "dum"), ==, 12345);
	g_assert_cmpint (fu_device_get_metadata_integer (device, "unknown"), ==, G_MAXUINT);

	/* broken integer */
	fu_device_set_metadata (device, "dum", "123junk");
	g_assert_cmpint (fu_device_get_metadata_integer (device, "dum"), ==, G_MAXUINT);
	fu_device_set_metadata (device, "huge", "4294967296"); /* not 32 bit */
	g_assert_cmpint (fu_device_get_metadata_integer (device, "huge"), ==, G_MAXUINT);
}

static void
fu_smbios_func (void)
{
	const gchar *str;
	gboolean ret;
	g_autofree gchar *dump = NULL;
	g_autoptr(FuSmbios) smbios = NULL;
	g_autoptr(GError) error = NULL;

	smbios = fu_smbios_new ();
	ret = fu_smbios_setup (smbios, &error);
	g_assert_no_error (error);
	g_assert (ret);
	dump = fu_smbios_to_string (smbios);
	if (g_getenv ("VERBOSE") != NULL)
		g_debug ("%s", dump);

	/* test for missing table */
	str = fu_smbios_get_string (smbios, 0xff, 0, &error);
	g_assert_error (error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_assert_null (str);
	g_clear_error (&error);

	/* check for invalid offset */
	str = fu_smbios_get_string (smbios, FU_SMBIOS_STRUCTURE_TYPE_BIOS, 0xff, &error);
	g_assert_error (error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_assert_null (str);
	g_clear_error (&error);

	/* get vendor */
	str = fu_smbios_get_string (smbios, FU_SMBIOS_STRUCTURE_TYPE_BIOS, 0x04, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (str, ==, "LENOVO");
}

static void
fu_smbios3_func (void)
{
	const gchar *str;
	gboolean ret;
	g_autofree gchar *path = NULL;
	g_autoptr(FuSmbios) smbios = NULL;
	g_autoptr(GError) error = NULL;

	path = fu_test_get_filename (TESTDATADIR, "dmi/tables64");
	g_assert_nonnull (path);

	smbios = fu_smbios_new ();
	ret = fu_smbios_setup_from_path (smbios, path, &error);
	g_assert_no_error (error);
	g_assert (ret);
	if (g_getenv ("VERBOSE") != NULL) {
		g_autofree gchar *dump = fu_smbios_to_string (smbios);
		g_debug ("%s", dump);
	}

	/* get vendor */
	str = fu_smbios_get_string (smbios, FU_SMBIOS_STRUCTURE_TYPE_BIOS, 0x04, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (str, ==, "Dell Inc.");
}

static void
fu_hwids_func (void)
{
	g_autoptr(FuHwids) hwids = NULL;
	g_autoptr(FuSmbios) smbios = NULL;
	g_autoptr(GError) error = NULL;
	gboolean ret;

	struct {
		const gchar *key;
		const gchar *value;
	} guids[] = {
		{ "Manufacturer",	"6de5d951-d755-576b-bd09-c5cf66b27234" },
		{ "HardwareID-14",	"6de5d951-d755-576b-bd09-c5cf66b27234" },
		{ "HardwareID-13",	"f8e1de5f-b68c-5f52-9d1a-f1ba52f1f773" },
		{ "HardwareID-12",	"5e820764-888e-529d-a6f9-dfd12bacb160" },
		{ "HardwareID-11",	"db73af4c-4612-50f7-b8a7-787cf4871847" },
		{ "HardwareID-10",	"f4275c1f-6130-5191-845c-3426247eb6a1" },
		{ "HardwareID-9",	"0cf8618d-9eff-537c-9f35-46861406eb9c" },
		{ "HardwareID-8",	"059eb22d-6dc7-59af-abd3-94bbe017f67c" },
		{ "HardwareID-7",	"da1da9b6-62f5-5f22-8aaa-14db7eeda2a4" },
		{ "HardwareID-6",	"178cd22d-ad9f-562d-ae0a-34009822cdbe" },
		{ "HardwareID-5",	"8dc9b7c5-f5d5-5850-9ab3-bd6f0549d814" },
		{ "HardwareID-4",	"660ccba8-1b78-5a33-80e6-9fb8354ee873" },
		{ "HardwareID-3",	"3faec92a-3ae3-5744-be88-495e90a7d541" },
		{ "HardwareID-2",	"705f45c6-fbca-5245-b9dd-6d4fab25e262" },
		{ "HardwareID-1",	"309d9985-e453-587e-8486-ff7c835a9ef2" },
		{ "HardwareID-0",	"d37363b8-5ec4-5725-b618-b75368a1ad28" },
		{ NULL, NULL }
	};

	smbios = fu_smbios_new ();
	ret = fu_smbios_setup (smbios, &error);
	g_assert_no_error (error);
	g_assert (ret);

	hwids = fu_hwids_new ();
	ret = fu_hwids_setup (hwids, smbios, &error);
	g_assert_no_error (error);
	g_assert (ret);

	g_assert_cmpstr (fu_hwids_get_value (hwids, FU_HWIDS_KEY_MANUFACTURER), ==,
			 "LENOVO");
	g_assert_cmpstr (fu_hwids_get_value (hwids, FU_HWIDS_KEY_ENCLOSURE_KIND), ==,
			 "10");
	g_assert_cmpstr (fu_hwids_get_value (hwids, FU_HWIDS_KEY_FAMILY), ==,
			 "ThinkPad T440s");
	g_assert_cmpstr (fu_hwids_get_value (hwids, FU_HWIDS_KEY_PRODUCT_NAME), ==,
			 "20ARS19C0C");
	g_assert_cmpstr (fu_hwids_get_value (hwids, FU_HWIDS_KEY_BIOS_VENDOR), ==,
			 "LENOVO");
	g_assert_cmpstr (fu_hwids_get_value (hwids, FU_HWIDS_KEY_BIOS_VERSION), ==,
			 "GJET75WW (2.25 )");
	g_assert_cmpstr (fu_hwids_get_value (hwids, FU_HWIDS_KEY_BIOS_MAJOR_RELEASE), ==, "2");
	g_assert_cmpstr (fu_hwids_get_value (hwids, FU_HWIDS_KEY_BIOS_MINOR_RELEASE), ==, "25");
	g_assert_cmpstr (fu_hwids_get_value (hwids, FU_HWIDS_KEY_PRODUCT_SKU), ==,
			 "LENOVO_MT_20AR_BU_Think_FM_ThinkPad T440s");
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
fu_plugin_quirks_func (void)
{
	const gchar *tmp;
	gboolean ret;
	g_autoptr(FuQuirks) quirks = fu_quirks_new ();
	g_autoptr(FuPlugin) plugin = fu_plugin_new ();
	g_autoptr(GError) error = NULL;

	ret = fu_quirks_load (quirks, &error);
	g_assert_no_error (error);
	g_assert (ret);
	fu_plugin_set_quirks (plugin, quirks);

	/* exact */
	tmp = fu_plugin_lookup_quirk_by_id (plugin, "fwupd-plugin-test", "USB\\VID_0A5C&PID_6412");
	g_assert_cmpstr (tmp, ==, "ignore-runtime");
	tmp = fu_plugin_lookup_quirk_by_id (plugin, "fwupd-plugin-test", "ACME Inc.");
	g_assert_cmpstr (tmp, ==, "awesome");
	tmp = fu_plugin_lookup_quirk_by_id (plugin, "fwupd-plugin-test", "CORP*");
	g_assert_cmpstr (tmp, ==, "town");
	tmp = fu_plugin_lookup_quirk_by_id (plugin, "fwupd-plugin-test", "USB\\VID_FFFF&PID_FFFF");
	g_assert_cmpstr (tmp, ==, "");
	tmp = fu_plugin_lookup_quirk_by_id (plugin, "fwupd-Unfound", "baz");
	g_assert_cmpstr (tmp, ==, NULL);
	tmp = fu_plugin_lookup_quirk_by_id (plugin, "fwupd-tests", "unfound");
	g_assert_cmpstr (tmp, ==, NULL);
	tmp = fu_plugin_lookup_quirk_by_id (plugin, "fwupd-unfound", "unfound");
	g_assert_cmpstr (tmp, ==, NULL);

	/* glob */
	tmp = fu_plugin_lookup_quirk_by_id (plugin, "fwupd-plugin-test", "ACME*");
	g_assert_cmpstr (tmp, ==, "awesome");
	tmp = fu_quirks_lookup_by_glob (quirks, "fwupd-plugin-test", "CORPORATION");
	g_assert_cmpstr (tmp, ==, "town");
	tmp = fu_plugin_lookup_quirk_by_id (plugin, "fwupd-plugin-test", "unfound*");
	g_assert_cmpstr (tmp, ==, NULL);
}

static void
fu_plugin_module_func (void)
{
	GError *error = NULL;
	FuDevice *device_tmp;
	gboolean ret;
	guint cnt = 0;
	g_autofree gchar *mapped_file_fn = NULL;
	g_autofree gchar *pending_cap = NULL;
	g_autofree gchar *pending_db = NULL;
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuDevice) device2 = NULL;
	g_autoptr(FuDevice) device3 = NULL;
	g_autoptr(FuPending) pending = NULL;
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(GBytes) blob_cab = NULL;
	g_autoptr(GMappedFile) mapped_file = NULL;

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
	device2 = fu_pending_get_device (pending, fu_device_get_id (device), &error);
	g_assert_no_error (error);
	g_assert (device2 != NULL);
	g_assert_cmpint (fu_device_get_update_state (device2), ==, FWUPD_UPDATE_STATE_PENDING);
	g_assert_cmpstr (fu_device_get_update_error (device2), ==, NULL);
	g_assert_cmpstr (fu_device_get_filename_pending (device2), !=, NULL);

	/* save this; we'll need to delete it later */
	pending_cap = g_strdup (fu_device_get_filename_pending (device2));

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
	device3 = fu_pending_get_device (pending, fu_device_get_id (device), &error);
	g_assert_no_error (error);
	g_assert (device3 != NULL);
	g_assert_cmpint (fu_device_get_update_state (device3), ==, FWUPD_UPDATE_STATE_SUCCESS);
	g_assert_cmpstr (fu_device_get_update_error (device3), ==, NULL);

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
	FuDevice *device;
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
	device = fu_device_new ();
	fu_device_set_id (device, "self-test");
	fu_device_set_filename_pending (device, "/var/lib/dave.cap"),
	fu_device_set_name (device, "ColorHug"),
	fu_device_set_version (device, "3.0.1"),
	fu_device_set_version_new (device, "3.0.2");
	ret = fu_pending_add_device (pending, device, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_object_unref (device);

	/* ensure database was created */
	g_assert (g_file_test (filename, G_FILE_TEST_EXISTS));

	/* add some extra data */
	device = fu_device_new ();
	fu_device_set_id (device, "self-test");
	ret = fu_pending_set_state (pending, device, FWUPD_UPDATE_STATE_PENDING, &error);
	g_assert_no_error (error);
	g_assert (ret);
	ret = fu_pending_set_error_msg (pending, device, "word", &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_object_unref (device);

	/* get device */
	device = fu_pending_get_device (pending, "self-test", &error);
	g_assert_no_error (error);
	g_assert (device != NULL);
	g_assert_cmpstr (fu_device_get_id (device), ==, "self-test");
	g_assert_cmpstr (fu_device_get_name (device), ==, "ColorHug");
	g_assert_cmpstr (fu_device_get_version (device), ==, "3.0.1");
	g_assert_cmpint (fu_device_get_update_state (device), ==, FWUPD_UPDATE_STATE_PENDING);
	g_assert_cmpstr (fu_device_get_update_error (device), ==, "word");
	g_assert_cmpstr (fu_device_get_filename_pending (device), ==, "/var/lib/dave.cap");
	g_assert_cmpstr (fu_device_get_version_new (device), ==, "3.0.2");
	g_object_unref (device);

	/* get device that does not exist */
	device = fu_pending_get_device (pending, "XXXXXXXXXXXXX", &error);
	g_assert_error (error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert (device == NULL);
	g_clear_error (&error);

	/* remove device */
	device = fu_device_new ();
	fu_device_set_id (device, "self-test");
	ret = fu_pending_remove_device (pending, device, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_object_unref (device);

	/* get device that does not exist */
	device = fu_pending_get_device (pending, "self-test", &error);
	g_assert_error (error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert (device == NULL);
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

static gboolean
_open_cb (GObject *device, GError **error)
{
	g_assert_cmpstr (g_object_get_data (device, "state"), ==, "closed");
	g_object_set_data (device, "state", "opened");
	return TRUE;
}

static gboolean
_close_cb (GObject *device, GError **error)
{
	g_assert_cmpstr (g_object_get_data (device, "state"), ==, "opened");
	g_object_set_data (device, "state", "closed-on-unref");
	return TRUE;
}

static void
fu_device_locker_func (void)
{
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GObject) device = g_object_new (G_TYPE_OBJECT, NULL);

	g_object_set_data (device, "state", "closed");
	locker = fu_device_locker_new_full (device, _open_cb, _close_cb, &error);
	g_assert_no_error (error);
	g_assert_nonnull (locker);
	g_clear_object (&locker);
	g_assert_cmpstr (g_object_get_data (device, "state"), ==, "closed-on-unref");
}

static gboolean
_fail_open_cb (GObject *device, GError **error)
{
	g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, "fail");
	return FALSE;
}

static gboolean
_fail_close_cb (GObject *device, GError **error)
{
	g_assert_not_reached ();
	return TRUE;
}

static void
fu_device_locker_fail_func (void)
{
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GObject) device = g_object_new (G_TYPE_OBJECT, NULL);
	locker = fu_device_locker_new_full (device, _fail_open_cb, _fail_close_cb, &error);
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_FAILED);
	g_assert_null (locker);
}

static void
fu_common_spawn_func (void)
{
	gboolean ret;
	guint lines = 0;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *fn = NULL;
	const gchar *argv[3] = { "replace", "test", NULL };

	fn = fu_test_get_filename (TESTDATADIR, "spawn.sh");
	g_assert (fn != NULL);
	argv[0] = fn;
	ret = fu_common_spawn_sync (argv,
				    fu_test_stdout_cb, &lines, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (lines, ==, 6);
}

static void
fu_progressbar_func (void)
{
	g_autoptr(FuProgressbar) progressbar = fu_progressbar_new ();

	fu_progressbar_set_length_status (progressbar, 20);
	fu_progressbar_set_length_percentage (progressbar, 50);

	g_print ("\n");
	for (guint i = 0; i < 100; i++) {
		fu_progressbar_update (progressbar, FWUPD_STATUS_DECOMPRESSING, i);
		g_usleep (10000);
	}
	fu_progressbar_update (progressbar, FWUPD_STATUS_IDLE, 0);
	for (guint i = 0; i < 100; i++) {
		guint pc = (i > 25 && i < 75) ? 0 : i;
		fu_progressbar_update (progressbar, FWUPD_STATUS_LOADING, pc);
		g_usleep (10000);
	}
	fu_progressbar_update (progressbar, FWUPD_STATUS_IDLE, 0);

	for (guint i = 0; i < 5000; i++) {
		fu_progressbar_update (progressbar, FWUPD_STATUS_LOADING, 0);
		g_usleep (1000);
	}
	fu_progressbar_update (progressbar, FWUPD_STATUS_IDLE, 0);
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
	if (g_test_slow ())
		g_test_add_func ("/fwupd/progressbar", fu_progressbar_func);
	g_test_add_func ("/fwupd/device-locker{success}", fu_device_locker_func);
	g_test_add_func ("/fwupd/device-locker{fail}", fu_device_locker_fail_func);
	g_test_add_func ("/fwupd/device{metadata}", fu_device_metadata_func);
	g_test_add_func ("/fwupd/engine", fu_engine_func);
	g_test_add_func ("/fwupd/engine{require-hwid}", fu_engine_require_hwid_func);
	g_test_add_func ("/fwupd/hwids", fu_hwids_func);
	g_test_add_func ("/fwupd/smbios", fu_smbios_func);
	g_test_add_func ("/fwupd/smbios3", fu_smbios3_func);
	g_test_add_func ("/fwupd/pending", fu_pending_func);
	g_test_add_func ("/fwupd/plugin{delay}", fu_plugin_delay_func);
	g_test_add_func ("/fwupd/plugin{module}", fu_plugin_module_func);
	g_test_add_func ("/fwupd/plugin{quirks}", fu_plugin_quirks_func);
	g_test_add_func ("/fwupd/keyring{gpg}", fu_keyring_gpg_func);
	g_test_add_func ("/fwupd/keyring{pkcs7}", fu_keyring_pkcs7_func);
	g_test_add_func ("/fwupd/common{spawn)", fu_common_spawn_func);
	g_test_add_func ("/fwupd/common{firmware-builder}", fu_common_firmware_builder_func);
	return g_test_run ();
}
