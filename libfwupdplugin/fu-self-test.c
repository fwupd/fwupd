/*
 * Copyright (C) 2015-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <xmlb.h>
#include <fwupd.h>
#include <fwupdplugin.h>
#include <libgcab.h>
#include <glib/gstdio.h>

#include "fu-device-private.h"
#include "fu-plugin-private.h"
#include "fu-smbios-private.h"

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

static void
fu_archive_invalid_func (void)
{
	g_autofree gchar *filename = NULL;
	g_autoptr(FuArchive) archive = NULL;
	g_autoptr(GBytes) data = NULL;
	g_autoptr(GError) error = NULL;

	filename = g_build_filename (TESTDATADIR_SRC, "metadata.xml", NULL);
	data = fu_common_get_contents_bytes (filename, &error);
	g_assert_no_error (error);
	g_assert_nonnull (data);

	archive = fu_archive_new (data, FU_ARCHIVE_FLAG_NONE, &error);
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED);
	g_assert_null (archive);
}

static void
fu_archive_cab_func (void)
{
	g_autofree gchar *checksum1 = NULL;
	g_autofree gchar *checksum2 = NULL;
	g_autofree gchar *filename = NULL;
	g_autoptr(FuArchive) archive = NULL;
	g_autoptr(GBytes) data = NULL;
	g_autoptr(GError) error = NULL;
	GBytes *data_tmp;

	filename = g_build_filename (TESTDATADIR_DST, "colorhug", "colorhug-als-3.0.2.cab", NULL);
	data = fu_common_get_contents_bytes (filename, &error);
	g_assert_no_error (error);
	g_assert_nonnull (data);

	archive = fu_archive_new (data, FU_ARCHIVE_FLAG_NONE, &error);
	g_assert_no_error (error);
	g_assert_nonnull (archive);

	data_tmp = fu_archive_lookup_by_fn (archive, "firmware.metainfo.xml", &error);
	g_assert_no_error (error);
	g_assert_nonnull (data_tmp);
	checksum1 = g_compute_checksum_for_bytes (G_CHECKSUM_SHA1, data_tmp);
	g_assert_cmpstr (checksum1, ==, "8611114f51f7151f190de86a5c9259d79ff34216");

	data_tmp = fu_archive_lookup_by_fn (archive, "firmware.bin", &error);
	g_assert_no_error (error);
	g_assert_nonnull (data_tmp);
	checksum2 = g_compute_checksum_for_bytes (G_CHECKSUM_SHA1, data_tmp);
	g_assert_cmpstr (checksum2, ==, "7c0ae84b191822bcadbdcbe2f74a011695d783c7");

	data_tmp = fu_archive_lookup_by_fn (archive, "NOTGOINGTOEXIST.xml", &error);
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
	g_assert_null (data_tmp);
}

static void
fu_common_string_append_kv_func (void)
{
	g_autoptr(GString) str = g_string_new (NULL);
	fu_common_string_append_kv (str, 0, "hdr", NULL);
	fu_common_string_append_kv (str, 0, "key", "value");
	fu_common_string_append_kv (str, 0, "key1", "value1");
	fu_common_string_append_kv (str, 1, "key2", "value2");
	fu_common_string_append_kv (str, 1, "", "value2");
	fu_common_string_append_kv (str, 2, "key3", "value3");
	g_assert_cmpstr (str->str, ==,
			 "hdr:\n"
			 "key:                     value\n"
			 "key1:                    value1\n"
			 "  key2:                  value2\n"
			 "                         value2\n"
			 "    key3:                value3\n");
}

static void
fu_common_version_guess_format_func (void)
{
	g_assert_cmpint (fu_common_version_guess_format (NULL), ==, FWUPD_VERSION_FORMAT_UNKNOWN);
	g_assert_cmpint (fu_common_version_guess_format (""), ==, FWUPD_VERSION_FORMAT_UNKNOWN);
	g_assert_cmpint (fu_common_version_guess_format ("1234ac"), ==, FWUPD_VERSION_FORMAT_PLAIN);
	g_assert_cmpint (fu_common_version_guess_format ("1.2"), ==, FWUPD_VERSION_FORMAT_PAIR);
	g_assert_cmpint (fu_common_version_guess_format ("1.2.3"), ==, FWUPD_VERSION_FORMAT_TRIPLET);
	g_assert_cmpint (fu_common_version_guess_format ("1.2.3.4"), ==, FWUPD_VERSION_FORMAT_QUAD);
	g_assert_cmpint (fu_common_version_guess_format ("1.2.3.4.5"), ==, FWUPD_VERSION_FORMAT_UNKNOWN);
	g_assert_cmpint (fu_common_version_guess_format ("1a.2b.3"), ==, FWUPD_VERSION_FORMAT_PLAIN);
	g_assert_cmpint (fu_common_version_guess_format ("1"), ==, FWUPD_VERSION_FORMAT_NUMBER);
	g_assert_cmpint (fu_common_version_guess_format ("0x10201"), ==, FWUPD_VERSION_FORMAT_NUMBER);
}

static void
fu_device_version_format_func (void)
{
	g_autoptr(FuDevice) device = fu_device_new ();
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_ENSURE_SEMVER);
	fu_device_set_version (device, "Ver1.2.3 RELEASE", FWUPD_VERSION_FORMAT_TRIPLET);
	g_assert_cmpstr (fu_device_get_version (device), ==, "1.2.3");
}

static void
fu_device_open_refcount_func (void)
{
	gboolean ret;
	g_autoptr(FuDevice) device = fu_device_new ();
	g_autoptr(GError) error = NULL;
	fu_device_set_id (device, "test_device");
	ret = fu_device_open (device, &error);
	g_assert_no_error (error);
	g_assert_true (ret);
	ret = fu_device_open (device, &error);
	g_assert_no_error (error);
	g_assert_true (ret);
	ret = fu_device_close (device, &error);
	g_assert_no_error (error);
	g_assert_true (ret);
	ret = fu_device_close (device, &error);
	g_assert_no_error (error);
	g_assert_true (ret);
	ret = fu_device_close (device, &error);
	g_assert_error (error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL);
	g_assert_false (ret);
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

	path = g_build_filename (TESTDATADIR_SRC, "dmi", "tables64", NULL);
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
		{ "HardwareID-12",	"e093d715-70f7-51f4-b6c8-b4a7e31def85" },
		{ "HardwareID-11",	"db73af4c-4612-50f7-b8a7-787cf4871847" },
		{ "HardwareID-10",	"f4275c1f-6130-5191-845c-3426247eb6a1" },
		{ "HardwareID-9",	"0cf8618d-9eff-537c-9f35-46861406eb9c" },
		{ "HardwareID-8",	"059eb22d-6dc7-59af-abd3-94bbe017f67c" },
		{ "HardwareID-7",	"da1da9b6-62f5-5f22-8aaa-14db7eeda2a4" },
		{ "HardwareID-6",	"178cd22d-ad9f-562d-ae0a-34009822cdbe" },
		{ "HardwareID-5",	"8dc9b7c5-f5d5-5850-9ab3-bd6f0549d814" },
		{ "HardwareID-4",	"660ccba8-1b78-5a33-80e6-9fb8354ee873" },
		{ "HardwareID-3",	"3faec92a-3ae3-5744-be88-495e90a7d541" },
		{ "HardwareID-2",	"f5ff077f-3eeb-5bae-be1c-e98ffe8ce5f8" },
		{ "HardwareID-1",	"b7cceb67-774c-537e-bf8b-22c6107e9a74" },
		{ "HardwareID-0",	"147efce9-f201-5fc8-ab0c-c859751c3440" },
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
			 "a");
	g_assert_cmpstr (fu_hwids_get_value (hwids, FU_HWIDS_KEY_FAMILY), ==,
			 "ThinkPad T440s");
	g_assert_cmpstr (fu_hwids_get_value (hwids, FU_HWIDS_KEY_PRODUCT_NAME), ==,
			 "20ARS19C0C");
	g_assert_cmpstr (fu_hwids_get_value (hwids, FU_HWIDS_KEY_BIOS_VENDOR), ==,
			 "LENOVO");
	g_assert_cmpstr (fu_hwids_get_value (hwids, FU_HWIDS_KEY_BIOS_VERSION), ==,
			 "GJET75WW (2.25 )");
	g_assert_cmpstr (fu_hwids_get_value (hwids, FU_HWIDS_KEY_BIOS_MAJOR_RELEASE), ==, "02");
	g_assert_cmpstr (fu_hwids_get_value (hwids, FU_HWIDS_KEY_BIOS_MINOR_RELEASE), ==, "19");
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
	g_assert_cmpstr (fu_device_get_id (device_tmp), ==, "b7eccd0059d6d7dc2ef76c35d6de0048cc8c029d");
	g_clear_object (&device_tmp);

	/* remove device */
	fu_plugin_device_remove (plugin, device);
	g_assert (device_tmp != NULL);
	g_assert_cmpstr (fu_device_get_id (device_tmp), ==, "b7eccd0059d6d7dc2ef76c35d6de0048cc8c029d");
	g_clear_object (&device_tmp);
}

static void
fu_plugin_quirks_func (void)
{
	const gchar *tmp;
	gboolean ret;
	g_autoptr(FuQuirks) quirks = fu_quirks_new ();
	g_autoptr(FuPlugin) plugin = fu_plugin_new ();
	g_autoptr(GError) error = NULL;

	ret = fu_quirks_load (quirks, FU_QUIRKS_LOAD_FLAG_NONE, &error);
	g_assert_no_error (error);
	g_assert (ret);
	fu_plugin_set_quirks (plugin, quirks);

	/* exact */
	tmp = fu_plugin_lookup_quirk_by_id (plugin, "USB\\VID_0A5C&PID_6412", "Flags");
	g_assert_cmpstr (tmp, ==, "ignore-runtime");
	tmp = fu_plugin_lookup_quirk_by_id (plugin, "ACME Inc.=True", "Test");
	g_assert_cmpstr (tmp, ==, "awesome");
	tmp = fu_plugin_lookup_quirk_by_id (plugin, "CORP*", "Test");
	g_assert_cmpstr (tmp, ==, "town");
	tmp = fu_plugin_lookup_quirk_by_id (plugin, "baz", "Unfound");
	g_assert_cmpstr (tmp, ==, NULL);
	tmp = fu_plugin_lookup_quirk_by_id (plugin, "unfound", "tests");
	g_assert_cmpstr (tmp, ==, NULL);
	tmp = fu_plugin_lookup_quirk_by_id (plugin, "unfound", "unfound");
	g_assert_cmpstr (tmp, ==, NULL);
	tmp = fu_plugin_lookup_quirk_by_id (plugin, "bb9ec3e2-77b3-53bc-a1f1-b05916715627", "Flags");
	g_assert_cmpstr (tmp, ==, "clever");
}

static void
fu_plugin_quirks_performance_func (void)
{
	gboolean ret;
	g_autoptr(FuQuirks) quirks = fu_quirks_new ();
	g_autoptr(GTimer) timer = g_timer_new ();
	g_autoptr(GError) error = NULL;
	const gchar *keys[] = { "Name", "Children", "Flags", NULL };

	ret = fu_quirks_load (quirks, FU_QUIRKS_LOAD_FLAG_NONE, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* lookup */
	g_timer_reset (timer);
	for (guint j = 0; j < 1000; j++) {
		const gchar *group = "DeviceInstanceId=USB\\VID_0BDA&PID_1100";
		for (guint i = 0; keys[i] != NULL; i++) {
			const gchar *tmp = fu_quirks_lookup_by_id (quirks, group, keys[i]);
			g_assert_cmpstr (tmp, !=, NULL);
		}
	}
	g_print ("lookup=%.3fms ", g_timer_elapsed (timer, NULL) * 1000.f);
}

static void
fu_plugin_quirks_device_func (void)
{
	FuDevice *device_tmp;
	GPtrArray *children;
	gboolean ret;
	g_autoptr(FuDevice) device = fu_device_new ();
	g_autoptr(FuQuirks) quirks = fu_quirks_new ();
	g_autoptr(GError) error = NULL;

	ret = fu_quirks_load (quirks, FU_QUIRKS_LOAD_FLAG_NONE, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* use quirk file to set device attributes */
	fu_device_set_physical_id (device, "usb:00:05");
	fu_device_set_quirks (device, quirks);
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_instance_id (device, "USB\\VID_0BDA&PID_1100");
	fu_device_convert_instance_ids (device);
	g_assert_cmpstr (fu_device_get_name (device), ==, "Hub");

	/* ensure children are created */
	children = fu_device_get_children (device);
	g_assert_cmpint (children->len, ==, 1);
	device_tmp = g_ptr_array_index (children, 0);
	g_assert_cmpstr (fu_device_get_name (device_tmp), ==, "HDMI");
	g_assert (fu_device_has_flag (device_tmp, FWUPD_DEVICE_FLAG_UPDATABLE));
}

static void fu_common_kernel_lockdown_func (void)
{
	gboolean ret;
	g_autofree gchar *old_kernel_dir = g_build_filename (TESTDATADIR_SRC,
							     "lockdown", NULL);
	g_autofree gchar *locked_dir = g_build_filename (TESTDATADIR_SRC,
							 "lockdown", "locked", NULL);
	g_autofree gchar *none_dir = g_build_filename (TESTDATADIR_SRC,
							"lockedown", "none", NULL);

	g_setenv ("FWUPD_SYSFSSECURITYDIR", old_kernel_dir, TRUE);
	ret = fu_common_kernel_locked_down ();
	g_assert_false (ret);

	g_setenv ("FWUPD_SYSFSSECURITYDIR", locked_dir, TRUE);
	ret = fu_common_kernel_locked_down ();
	g_assert_true (ret);

	g_setenv ("FWUPD_SYSFSSECURITYDIR", none_dir, TRUE);
	ret = fu_common_kernel_locked_down ();
	g_assert_false (ret);
}

static void
fu_common_firmware_builder_func (void)
{
	const gchar *data;
	g_autofree gchar *archive_fn = NULL;
	g_autoptr(GBytes) archive_blob = NULL;
	g_autoptr(GBytes) firmware_blob = NULL;
	g_autoptr(GError) error = NULL;

	/* get test file */
	archive_fn = g_build_filename (TESTDATADIR_DST, "builder", "firmware.tar", NULL);
	archive_blob = fu_common_get_contents_bytes (archive_fn, &error);
	g_assert_no_error (error);
	g_assert (archive_blob != NULL);

	/* generate the firmware */
	firmware_blob = fu_common_firmware_builder (archive_blob,
						    "startup.sh",
						    "firmware.bin",
						    &error);
	if (firmware_blob == NULL) {
		if (g_error_matches (error, FWUPD_ERROR, FWUPD_ERROR_PERMISSION_DENIED)) {
			g_test_skip ("Missing permissions to create namespace in container");
			return;
		}
		if (g_error_matches (error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED)) {
			g_test_skip ("User namespaces not supported in container");
			return;
		}
		g_assert_no_error (error);
	}

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
	g_object_set_data (device, "state", (gpointer) "opened");
	return TRUE;
}

static gboolean
_close_cb (GObject *device, GError **error)
{
	g_assert_cmpstr (g_object_get_data (device, "state"), ==, "opened");
	g_object_set_data (device, "state", (gpointer) "closed-on-unref");
	return TRUE;
}

static void
fu_device_locker_func (void)
{
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GObject) device = g_object_new (G_TYPE_OBJECT, NULL);

	g_object_set_data (device, "state", (gpointer) "closed");
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

#ifdef _WIN32
	g_test_skip ("Known failures on Windows right now, skipping spawn func test");
	return;
#endif

	fn = g_build_filename (TESTDATADIR_SRC, "spawn.sh", NULL);
	argv[0] = fn;
	ret = fu_common_spawn_sync (argv,
				    fu_test_stdout_cb, &lines, 0, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (lines, ==, 6);
}

static void
fu_common_spawn_timeout_func (void)
{
	gboolean ret;
	guint lines = 0;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *fn = NULL;
	const gchar *argv[3] = { "replace", "test", NULL };

#ifdef _WIN32
	g_test_skip ("Known failures on Windows right now, skipping spawn timeout test");
	return;
#endif

	fn = g_build_filename (TESTDATADIR_SRC, "spawn.sh", NULL);
	argv[0] = fn;
	ret = fu_common_spawn_sync (argv, fu_test_stdout_cb, &lines, 50, NULL, &error);
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
	g_assert (!ret);
	g_assert_cmpint (lines, ==, 1);
}

static void
fu_common_endian_func (void)
{
	guint8 buf[2];

	fu_common_write_uint16 (buf, 0x1234, G_LITTLE_ENDIAN);
	g_assert_cmpint (buf[0], ==, 0x34);
	g_assert_cmpint (buf[1], ==, 0x12);
	g_assert_cmpint (fu_common_read_uint16 (buf, G_LITTLE_ENDIAN), ==, 0x1234);

	fu_common_write_uint16 (buf, 0x1234, G_BIG_ENDIAN);
	g_assert_cmpint (buf[0], ==, 0x12);
	g_assert_cmpint (buf[1], ==, 0x34);
	g_assert_cmpint (fu_common_read_uint16 (buf, G_BIG_ENDIAN), ==, 0x1234);
}

static GBytes *
_build_cab (GCabCompression compression, ...)
{
#ifdef HAVE_GCAB_1_0
	gboolean ret;
	va_list args;
	g_autoptr(GCabCabinet) cabinet = NULL;
	g_autoptr(GCabFolder) cabfolder = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GOutputStream) op = NULL;

	/* create a new archive */
	cabinet = gcab_cabinet_new ();
	cabfolder = gcab_folder_new (compression);
	ret = gcab_cabinet_add_folder (cabinet, cabfolder, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* add each file */
	va_start (args, compression);
	do {
		const gchar *fn;
		const gchar *text;
		g_autoptr(GCabFile) cabfile = NULL;
		g_autoptr(GBytes) blob = NULL;

		/* get filename */
		fn = va_arg (args, const gchar *);
		if (fn == NULL)
			break;

		/* get contents */
		text = va_arg (args, const gchar *);
		if (text == NULL)
			break;
		g_debug ("creating %s with %s", fn, text);

		/* add a GCabFile to the cabinet */
		blob = g_bytes_new_static (text, strlen (text));
		cabfile = gcab_file_new_with_bytes (fn, blob);
		ret = gcab_folder_add_file (cabfolder, cabfile, FALSE, NULL, &error);
		g_assert_no_error (error);
		g_assert (ret);
	} while (TRUE);
	va_end (args);

	/* write the archive to a blob */
	op = g_memory_output_stream_new_resizable ();
	ret = gcab_cabinet_write_simple  (cabinet, op, NULL, NULL, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);
	ret = g_output_stream_close (op, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);
	return g_memory_output_stream_steal_as_bytes (G_MEMORY_OUTPUT_STREAM (op));
#else
	return NULL;
#endif
}

static void
fu_common_store_cab_func (void)
{
	GBytes *blob_tmp;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbNode) csum = NULL;
	g_autoptr(XbNode) rel = NULL;
	g_autoptr(XbNode) req = NULL;
	g_autoptr(XbSilo) silo = NULL;

	/* create silo */
	blob = _build_cab (GCAB_COMPRESSION_NONE,
			   "acme.metainfo.xml",
	"<component type=\"firmware\">\n"
	"  <id>com.acme.example.firmware</id>\n"
	"  <name>ACME Firmware</name>\n"
	"  <provides>\n"
	"    <firmware type=\"flashed\">ae56e3fb-6528-5bc4-8b03-012f124075d7</firmware>\n"
	"  </provides>\n"
	"  <releases>\n"
	"    <release version=\"1.2.3\" date=\"2017-09-06\">\n"
	"      <size type=\"installed\">5</size>\n"
	"      <checksum filename=\"firmware.dfu\" target=\"content\" type=\"sha1\">7c211433f02071597741e6ff5a8ea34789abbf43</checksum>\n"
	"      <description><p>We fixed things</p></description>\n"
	"    </release>\n"
	"  </releases>\n"
	"  <requires>\n"
	"    <id compare=\"ge\" version=\"1.0.1\">org.freedesktop.fwupd</id>\n"
	"  </requires>\n"
	"</component>",
			   "firmware.dfu", "world",
			   "firmware.dfu.asc", "signature",
			   NULL);
	if (blob == NULL) {
		g_test_skip ("libgcab too old");
		return;
	}
	silo = fu_common_cab_build_silo (blob, 10240, &error);
	g_assert_no_error (error);
	g_assert_nonnull (silo);

	/* verify */
	component = xb_silo_query_first (silo, "components/component/id[text()='com.acme.example.firmware']/..", &error);
	g_assert_no_error (error);
	g_assert_nonnull (component);
	rel = xb_node_query_first (component, "releases/release", &error);
	g_assert_no_error (error);
	g_assert_nonnull (rel);
	g_assert_cmpstr (xb_node_get_attr (rel, "version"), ==, "1.2.3");
	csum = xb_node_query_first (rel, "checksum[@target='content']", &error);
	g_assert_nonnull (csum);
	g_assert_cmpstr (xb_node_get_text (csum), ==, "7c211433f02071597741e6ff5a8ea34789abbf43");
	blob_tmp = xb_node_get_data (rel, "fwupd::ReleaseBlob(firmware.dfu)");
	g_assert_nonnull (blob_tmp);
	blob_tmp = xb_node_get_data (rel, "fwupd::ReleaseBlob(firmware.dfu.asc)");
	g_assert_nonnull (blob_tmp);
	req = xb_node_query_first (component, "requires/id", &error);
	g_assert_no_error (error);
	g_assert_nonnull (req);
}

static void
fu_common_store_cab_unsigned_func (void)
{
	GBytes *blob_tmp;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbNode) csum = NULL;
	g_autoptr(XbNode) rel = NULL;
	g_autoptr(XbSilo) silo = NULL;

	/* create silo */
	blob = _build_cab (GCAB_COMPRESSION_NONE,
			   "acme.metainfo.xml",
	"<component type=\"firmware\">\n"
	"  <id>com.acme.example.firmware</id>\n"
	"  <releases>\n"
	"    <release version=\"1.2.3\"/>\n"
	"  </releases>\n"
	"</component>",
			   "firmware.bin", "world",
			   NULL);
	if (blob == NULL) {
		g_test_skip ("libgcab too old");
		return;
	}
	silo = fu_common_cab_build_silo (blob, 10240, &error);
	g_assert_no_error (error);
	g_assert_nonnull (silo);

	/* verify */
	component = xb_silo_query_first (silo, "components/component/id[text()='com.acme.example.firmware']/..", &error);
	g_assert_no_error (error);
	g_assert_nonnull (component);
	rel = xb_node_query_first (component, "releases/release", &error);
	g_assert_no_error (error);
	g_assert_nonnull (rel);
	g_assert_cmpstr (xb_node_get_attr (rel, "version"), ==, "1.2.3");
	csum = xb_node_query_first (rel, "checksum[@target='content']", &error);
	g_assert_null (csum);
	blob_tmp = xb_node_get_data (rel, "fwupd::ReleaseBlob(firmware.bin)");
	g_assert_nonnull (blob_tmp);
	blob_tmp = xb_node_get_data (rel, "fwupd::ReleaseBlob(firmware.bin.asc)");
	g_assert_null (blob_tmp);
}

static void
fu_common_store_cab_folder_func (void)
{
	GBytes *blob_tmp;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbNode) rel = NULL;
	g_autoptr(XbSilo) silo = NULL;

	/* create silo */
	blob = _build_cab (GCAB_COMPRESSION_NONE,
			   "lvfs\\acme.metainfo.xml",
	"<component type=\"firmware\">\n"
	"  <id>com.acme.example.firmware</id>\n"
	"  <releases>\n"
	"    <release version=\"1.2.3\"/>\n"
	"  </releases>\n"
	"</component>",
			   "lvfs\\firmware.bin", "world",
			   NULL);
	if (blob == NULL) {
		g_test_skip ("libgcab too old");
		return;
	}
	silo = fu_common_cab_build_silo (blob, 10240, &error);
	g_assert_no_error (error);
	g_assert_nonnull (silo);

	/* verify */
	component = xb_silo_query_first (silo, "components/component/id[text()='com.acme.example.firmware']/..", &error);
	g_assert_no_error (error);
	g_assert_nonnull (component);
	rel = xb_node_query_first (component, "releases/release", &error);
	g_assert_no_error (error);
	g_assert_nonnull (rel);
	g_assert_cmpstr (xb_node_get_attr (rel, "version"), ==, "1.2.3");
	blob_tmp = xb_node_get_data (rel, "fwupd::ReleaseBlob(firmware.bin)");
	g_assert_nonnull (blob_tmp);
}

static void
fu_common_store_cab_error_no_metadata_func (void)
{
	g_autoptr(XbSilo) silo = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;

	blob = _build_cab (GCAB_COMPRESSION_NONE,
			   "foo.txt", "hello",
			   "bar.txt", "world",
			   NULL);
	if (blob == NULL) {
		g_test_skip ("libgcab too old");
		return;
	}
	silo = fu_common_cab_build_silo (blob, 10240, &error);
	g_assert_error (error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_assert_null (silo);
}

static void
fu_common_store_cab_error_wrong_size_func (void)
{
	g_autoptr(XbSilo) silo = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;

	blob = _build_cab (GCAB_COMPRESSION_NONE,
			   "acme.metainfo.xml",
	"<component type=\"firmware\">\n"
	"  <id>com.acme.example.firmware</id>\n"
	"  <releases>\n"
	"    <release version=\"1.2.3\">\n"
	"      <size type=\"installed\">7004701</size>\n"
	"      <checksum filename=\"firmware.bin\" target=\"content\" type=\"sha1\">deadbeef</checksum>\n"
	"    </release>\n"
	"  </releases>\n"
	"</component>",
			   "firmware.bin", "world",
			   NULL);
	if (blob == NULL) {
		g_test_skip ("libgcab too old");
		return;
	}
	silo = fu_common_cab_build_silo (blob, 10240, &error);
	g_assert_error (error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_assert_null (silo);
}

static void
fu_common_store_cab_error_missing_file_func (void)
{
	g_autoptr(XbSilo) silo = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;

	blob = _build_cab (GCAB_COMPRESSION_NONE,
			   "acme.metainfo.xml",
	"<component type=\"firmware\">\n"
	"  <id>com.acme.example.firmware</id>\n"
	"  <releases>\n"
	"    <release version=\"1.2.3\">\n"
	"      <checksum filename=\"firmware.dfu\" target=\"content\"/>\n"
	"    </release>\n"
	"  </releases>\n"
	"</component>",
			   "firmware.bin", "world",
			   NULL);
	if (blob == NULL) {
		g_test_skip ("libgcab too old");
		return;
	}
	silo = fu_common_cab_build_silo (blob, 10240, &error);
	g_assert_error (error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_assert_null (silo);
}

static void
fu_common_store_cab_error_size_func (void)
{
	g_autoptr(XbSilo) silo = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;

	blob = _build_cab (GCAB_COMPRESSION_NONE,
			   "acme.metainfo.xml",
	"<component type=\"firmware\">\n"
	"  <id>com.acme.example.firmware</id>\n"
	"  <releases>\n"
	"    <release version=\"1.2.3\"/>\n"
	"  </releases>\n"
	"</component>",
			   "firmware.bin", "world",
			   NULL);
	if (blob == NULL) {
		g_test_skip ("libgcab too old");
		return;
	}
	silo = fu_common_cab_build_silo (blob, 123, &error);
	g_assert_error (error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_assert_null (silo);
}

static void
fu_common_store_cab_error_wrong_checksum_func (void)
{
	g_autoptr(XbSilo) silo = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;

	blob = _build_cab (GCAB_COMPRESSION_NONE,
			   "acme.metainfo.xml",
	"<component type=\"firmware\">\n"
	"  <id>com.acme.example.firmware</id>\n"
	"  <releases>\n"
	"    <release version=\"1.2.3\">\n"
	"      <checksum filename=\"firmware.bin\" target=\"content\" type=\"sha1\">deadbeef</checksum>\n"
	"    </release>\n"
	"  </releases>\n"
	"</component>",
			   "firmware.bin", "world",
			   NULL);
	if (blob == NULL) {
		g_test_skip ("libgcab too old");
		return;
	}
	silo = fu_common_cab_build_silo (blob, 10240, &error);
	g_assert_error (error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_assert_null (silo);
}

static gboolean
fu_device_poll_cb (FuDevice *device, GError **error)
{
	guint64 cnt = fu_device_get_metadata_integer (device, "cnt");
	g_debug ("poll cnt=%" G_GUINT64_FORMAT, cnt);
	fu_device_set_metadata_integer (device, "cnt", cnt + 1);
	return TRUE;
}

static void
fu_device_poll_func (void)
{
	g_autoptr(FuDevice) device = fu_device_new ();
	FuDeviceClass *klass = FU_DEVICE_GET_CLASS (device);
	guint cnt;

	/* set up a 10ms poll */
	klass->poll = fu_device_poll_cb;
	fu_device_set_metadata_integer (device, "cnt", 0);
	fu_device_set_poll_interval (device, 10);
	fu_test_loop_run_with_timeout (100);
	fu_test_loop_quit ();
	cnt = fu_device_get_metadata_integer (device, "cnt");
	g_assert_cmpint (cnt, >=, 8);

	/* disable the poll */
	fu_device_set_poll_interval (device, 0);
	fu_test_loop_run_with_timeout (100);
	fu_test_loop_quit ();
	g_assert_cmpint (fu_device_get_metadata_integer (device, "cnt"), ==, cnt);
}

static void
fu_device_incorporate_func (void)
{
	g_autoptr(FuDevice) device = fu_device_new ();
	g_autoptr(FuDevice) donor = fu_device_new ();

	/* set up donor device */
	fu_device_set_alternate_id (donor, "alt-id");
	fu_device_set_equivalent_id (donor, "equiv-id");
	fu_device_set_metadata (donor, "test", "me");
	fu_device_set_metadata (donor, "test2", "me");

	/* base properties */
	fu_device_add_flag (donor, FWUPD_DEVICE_FLAG_REQUIRE_AC);
	fu_device_set_created (donor, 123);
	fu_device_set_modified (donor, 456);
	fu_device_add_icon (donor, "computer");

	/* existing properties */
	fu_device_set_equivalent_id (device, "DO_NOT_OVERWRITE");
	fu_device_set_metadata (device, "test2", "DO_NOT_OVERWRITE");
	fu_device_set_modified (device, 789);

	/* incorporate properties from donor to device */
	fu_device_incorporate (device, donor);
	g_assert_cmpstr (fu_device_get_alternate_id (device), ==, "alt-id");
	g_assert_cmpstr (fu_device_get_equivalent_id (device), ==, "DO_NOT_OVERWRITE");
	g_assert_cmpstr (fu_device_get_metadata (device, "test"), ==, "me");
	g_assert_cmpstr (fu_device_get_metadata (device, "test2"), ==, "DO_NOT_OVERWRITE");
	g_assert_true (fu_device_has_flag (device, FWUPD_DEVICE_FLAG_REQUIRE_AC));
	g_assert_cmpint (fu_device_get_created (device), ==, 123);
	g_assert_cmpint (fu_device_get_modified (device), ==, 789);
	g_assert_cmpint (fu_device_get_icons(device)->len, ==, 1);
}

static void
fu_chunk_func (void)
{
	g_autofree gchar *chunked1_str = NULL;
	g_autofree gchar *chunked2_str = NULL;
	g_autofree gchar *chunked3_str = NULL;
	g_autofree gchar *chunked4_str = NULL;
	g_autoptr(GPtrArray) chunked1 = NULL;
	g_autoptr(GPtrArray) chunked2 = NULL;
	g_autoptr(GPtrArray) chunked3 = NULL;
	g_autoptr(GPtrArray) chunked4 = NULL;

	chunked3 = fu_chunk_array_new ((const guint8 *) "123456", 6, 0x0, 3, 3);
	chunked3_str = fu_chunk_array_to_string (chunked3);
	g_print ("\n%s", chunked3_str);
	g_assert_cmpstr (chunked3_str, ==, "#00: page:00 addr:0000 len:03 123\n"
					   "#01: page:01 addr:0000 len:03 456\n");

	chunked4 = fu_chunk_array_new ((const guint8 *) "123456", 6, 0x4, 4, 4);
	chunked4_str = fu_chunk_array_to_string (chunked4);
	g_print ("\n%s", chunked4_str);
	g_assert_cmpstr (chunked4_str, ==, "#00: page:01 addr:0000 len:04 1234\n"
					   "#01: page:02 addr:0000 len:02 56\n");

	chunked1 = fu_chunk_array_new ((const guint8 *) "0123456789abcdef", 16, 0x0, 10, 4);
	chunked1_str = fu_chunk_array_to_string (chunked1);
	g_print ("\n%s", chunked1_str);
	g_assert_cmpstr (chunked1_str, ==, "#00: page:00 addr:0000 len:04 0123\n"
					   "#01: page:00 addr:0004 len:04 4567\n"
					   "#02: page:00 addr:0008 len:02 89\n"
					   "#03: page:01 addr:0000 len:04 abcd\n"
					   "#04: page:01 addr:0004 len:02 ef\n");

	chunked2 = fu_chunk_array_new ((const guint8 *) "XXXXXXYYYYYYZZZZZZ", 18, 0x0, 6, 4);
	chunked2_str = fu_chunk_array_to_string (chunked2);
	g_print ("\n%s", chunked2_str);
	g_assert_cmpstr (chunked2_str, ==, "#00: page:00 addr:0000 len:04 XXXX\n"
					   "#01: page:00 addr:0004 len:02 XX\n"
					   "#02: page:01 addr:0000 len:04 YYYY\n"
					   "#03: page:01 addr:0004 len:02 YY\n"
					   "#04: page:02 addr:0000 len:04 ZZZZ\n"
					   "#05: page:02 addr:0004 len:02 ZZ\n");
}

static void
fu_common_strstrip_func (void)
{

	struct {
		const gchar *old;
		const gchar *new;
	} map[] = {
		{ "same", "same" },
		{ " leading", "leading" },
		{ "tailing ", "tailing" },
		{ "  b  ", "b" },
		{ "  ", "" },
		{ NULL, NULL }
	};
	for (guint i = 0; map[i].old != NULL; i++) {
		g_autofree gchar *tmp = fu_common_strstrip (map[i].old);
		g_assert_cmpstr (tmp, ==, map[i].new);
	}
}

static void
fu_common_version_func (void)
{
	guint i;
	struct {
		guint32 val;
		const gchar *ver;
		FwupdVersionFormat flags;
	} version_from_uint32[] = {
		{ 0x0,		"0.0.0.0",		FWUPD_VERSION_FORMAT_QUAD },
		{ 0xff,		"0.0.0.255",		FWUPD_VERSION_FORMAT_QUAD },
		{ 0xff01,	"0.0.255.1",		FWUPD_VERSION_FORMAT_QUAD },
		{ 0xff0001,	"0.255.0.1",		FWUPD_VERSION_FORMAT_QUAD },
		{ 0xff000100,	"255.0.1.0",		FWUPD_VERSION_FORMAT_QUAD },
		{ 0x0,		"0.0.0",		FWUPD_VERSION_FORMAT_TRIPLET },
		{ 0xff,		"0.0.255",		FWUPD_VERSION_FORMAT_TRIPLET },
		{ 0xff01,	"0.0.65281",		FWUPD_VERSION_FORMAT_TRIPLET },
		{ 0xff0001,	"0.255.1",		FWUPD_VERSION_FORMAT_TRIPLET },
		{ 0xff000100,	"255.0.256",		FWUPD_VERSION_FORMAT_TRIPLET },
		{ 0x0,		"0",			FWUPD_VERSION_FORMAT_NUMBER },
		{ 0xff000100,	"4278190336",		FWUPD_VERSION_FORMAT_NUMBER },
		{ 0x0,		"11.0.0.0",		FWUPD_VERSION_FORMAT_INTEL_ME },
		{ 0xffffffff,	"18.31.255.65535",	FWUPD_VERSION_FORMAT_INTEL_ME },
		{ 0x0b32057a,	"11.11.50.1402",	FWUPD_VERSION_FORMAT_INTEL_ME },
		{ 0xb8320d84,	"11.8.50.3460",		FWUPD_VERSION_FORMAT_INTEL_ME2 },
		{ 0x226a4b00,	"137.2706.768",		FWUPD_VERSION_FORMAT_SURFACE_LEGACY },
		{ 0x6001988,	"6.25.136",		FWUPD_VERSION_FORMAT_SURFACE },
		{ 0x00ff0001,	"255.0.1",		FWUPD_VERSION_FORMAT_DELL_BIOS },
		{ 0,		NULL }
	};
	struct {
		guint64 val;
		const gchar *ver;
		FwupdVersionFormat flags;
	} version_from_uint64[] = {
		{ 0x0,		"0.0.0.0",		FWUPD_VERSION_FORMAT_QUAD },
		{ 0xff,		"0.0.0.255",		FWUPD_VERSION_FORMAT_QUAD },
		{ 0xffffffffffffffff, "65535.65535.65535.65535", FWUPD_VERSION_FORMAT_QUAD },
		{ 0xff,		"0.255",		FWUPD_VERSION_FORMAT_PAIR },
		{ 0xffffffffffffffff, "4294967295.4294967295", FWUPD_VERSION_FORMAT_PAIR },
		{ 0x0,		"0",			FWUPD_VERSION_FORMAT_NUMBER },
		{ 0,		NULL }
	};
	struct {
		guint16 val;
		const gchar *ver;
		FwupdVersionFormat flags;
	} version_from_uint16[] = {
		{ 0x0,		"0.0",			FWUPD_VERSION_FORMAT_PAIR },
		{ 0xff,		"0.255",		FWUPD_VERSION_FORMAT_PAIR },
		{ 0xff01,	"255.1",		FWUPD_VERSION_FORMAT_PAIR },
		{ 0x0,		"0.0",			FWUPD_VERSION_FORMAT_BCD },
		{ 0x0110,	"1.10",			FWUPD_VERSION_FORMAT_BCD },
		{ 0x9999,	"99.99",		FWUPD_VERSION_FORMAT_BCD },
		{ 0x0,		"0",			FWUPD_VERSION_FORMAT_NUMBER },
		{ 0x1234,	"4660",			FWUPD_VERSION_FORMAT_NUMBER },
		{ 0,		NULL }
	};
	struct {
		const gchar *old;
		const gchar *new;
	} version_parse[] = {
		{ "0",		"0" },
		{ "0x1a",	"0.0.26" },
		{ "257",	"0.0.257" },
		{ "1.2.3",	"1.2.3" },
		{ "0xff0001",	"0.255.1" },
		{ "16711681",	"0.255.1" },
		{ "20150915",	"20150915" },
		{ "dave",	"dave" },
		{ "0x1x",	"0x1x" },
		{ NULL,		NULL }
	};

	/* check version conversion */
	for (i = 0; version_from_uint64[i].ver != NULL; i++) {
		g_autofree gchar *ver = NULL;
		ver = fu_common_version_from_uint64 (version_from_uint64[i].val,
						    version_from_uint64[i].flags);
		g_assert_cmpstr (ver, ==, version_from_uint64[i].ver);
	}
	for (i = 0; version_from_uint32[i].ver != NULL; i++) {
		g_autofree gchar *ver = NULL;
		ver = fu_common_version_from_uint32 (version_from_uint32[i].val,
						    version_from_uint32[i].flags);
		g_assert_cmpstr (ver, ==, version_from_uint32[i].ver);
	}
	for (i = 0; version_from_uint16[i].ver != NULL; i++) {
		g_autofree gchar *ver = NULL;
		ver = fu_common_version_from_uint16 (version_from_uint16[i].val,
						    version_from_uint16[i].flags);
		g_assert_cmpstr (ver, ==, version_from_uint16[i].ver);
	}

	/* check version parsing */
	for (i = 0; version_parse[i].old != NULL; i++) {
		g_autofree gchar *ver = NULL;
		ver = fu_common_version_parse_from_format (version_parse[i].old,
							   FWUPD_VERSION_FORMAT_TRIPLET);
		g_assert_cmpstr (ver, ==, version_parse[i].new);
	}
}

static void
fu_common_vercmp_func (void)
{
	/* same */
	g_assert_cmpint (fu_common_vercmp ("1.2.3", "1.2.3"), ==, 0);
	g_assert_cmpint (fu_common_vercmp ("001.002.003", "001.002.003"), ==, 0);

	/* upgrade and downgrade */
	g_assert_cmpint (fu_common_vercmp ("1.2.3", "1.2.4"), <, 0);
	g_assert_cmpint (fu_common_vercmp ("001.002.000", "001.002.009"), <, 0);
	g_assert_cmpint (fu_common_vercmp ("1.2.3", "1.2.2"), >, 0);
	g_assert_cmpint (fu_common_vercmp ("001.002.009", "001.002.000"), >, 0);

	/* unequal depth */
	g_assert_cmpint (fu_common_vercmp ("1.2.3", "1.2.3.1"), <, 0);
	g_assert_cmpint (fu_common_vercmp ("1.2.3.1", "1.2.4"), <, 0);

	/* mixed-alpha-numeric */
	g_assert_cmpint (fu_common_vercmp ("1.2.3a", "1.2.3a"), ==, 0);
	g_assert_cmpint (fu_common_vercmp ("1.2.3a", "1.2.3b"), <, 0);
	g_assert_cmpint (fu_common_vercmp ("1.2.3b", "1.2.3a"), >, 0);

	/* alpha version append */
	g_assert_cmpint (fu_common_vercmp ("1.2.3", "1.2.3a"), <, 0);
	g_assert_cmpint (fu_common_vercmp ("1.2.3a", "1.2.3"), >, 0);

	/* alpha only */
	g_assert_cmpint (fu_common_vercmp ("alpha", "alpha"), ==, 0);
	g_assert_cmpint (fu_common_vercmp ("alpha", "beta"), <, 0);
	g_assert_cmpint (fu_common_vercmp ("beta", "alpha"), >, 0);

	/* alpha-compare */
	g_assert_cmpint (fu_common_vercmp ("1.2a.3", "1.2a.3"), ==, 0);
	g_assert_cmpint (fu_common_vercmp ("1.2a.3", "1.2b.3"), <, 0);
	g_assert_cmpint (fu_common_vercmp ("1.2b.3", "1.2a.3"), >, 0);

	/* tilde is all-powerful */
	g_assert_cmpint (fu_common_vercmp ("1.2.3~rc1", "1.2.3~rc1"), ==, 0);
	g_assert_cmpint (fu_common_vercmp ("1.2.3~rc1", "1.2.3"), <, 0);
	g_assert_cmpint (fu_common_vercmp ("1.2.3", "1.2.3~rc1"), >, 0);
	g_assert_cmpint (fu_common_vercmp ("1.2.3~rc2", "1.2.3~rc1"), >, 0);

	/* invalid */
	g_assert_cmpint (fu_common_vercmp ("1", NULL), ==, G_MAXINT);
	g_assert_cmpint (fu_common_vercmp (NULL, "1"), ==, G_MAXINT);
	g_assert_cmpint (fu_common_vercmp (NULL, NULL), ==, G_MAXINT);
}

static void
fu_firmware_ihex_func (void)
{
	const guint8 *data;
	gboolean ret;
	gsize len;
	g_autofree gchar *filename_hex = NULL;
	g_autofree gchar *filename_ref = NULL;
	g_autofree gchar *str = NULL;
	g_autoptr(FuFirmware) firmware = fu_ihex_firmware_new ();
	g_autoptr(GBytes) data_file = NULL;
	g_autoptr(GBytes) data_fw = NULL;
	g_autoptr(GBytes) data_hex = NULL;
	g_autoptr(GBytes) data_ref = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GFile) file_ref = NULL;
	g_autoptr(GFile) file_hex = NULL;

	/* load a Intel hex32 file */
	filename_hex = g_build_filename (TESTDATADIR_SRC, "firmware.hex", NULL);
	file_hex = g_file_new_for_path (filename_hex);
	data_file = g_file_load_bytes (file_hex, NULL, NULL, &error);
	g_assert_no_error (error);
	g_assert (data_file != NULL);
	ret = fu_firmware_parse (firmware, data_file, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error (error);
	g_assert (ret);
	data_fw = fu_firmware_get_image_default_bytes (firmware, &error);
	g_assert_no_error (error);
	g_assert_nonnull (data_fw);
	g_assert_cmpint (g_bytes_get_size (data_fw), ==, 136);

	/* did we match the reference file? */
	filename_ref = g_build_filename (TESTDATADIR_SRC, "firmware.bin", NULL);
	file_ref = g_file_new_for_path (filename_ref);
	data_ref = g_file_load_bytes (file_ref, NULL, NULL, &error);
	g_assert_no_error (error);
	g_assert (data_ref != NULL);
	ret = fu_common_bytes_compare (data_fw, data_ref, &error);
	g_assert_no_error (error);
	g_assert_true (ret);

	/* export a ihex file (which will be slightly different due to
	 * non-continous regions being expanded */
	data_hex = fu_firmware_write (firmware, &error);
	g_assert_no_error (error);
	g_assert (data_hex != NULL);
	data = g_bytes_get_data (data_hex, &len);
	str = g_strndup ((const gchar *) data, len);
	g_assert_cmpstr (str, ==,
			 ":104000003DEF20F000000000FACF01F0FBCF02F0FE\n"
			 ":10401000E9CF03F0EACF04F0E1CF05F0E2CF06F0FC\n"
			 ":10402000D9CF07F0DACF08F0F3CF09F0F4CF0AF0D8\n"
			 ":10403000F6CF0BF0F7CF0CF0F8CF0DF0F5CF0EF078\n"
			 ":104040000EC0F5FF0DC0F8FF0CC0F7FF0BC0F6FF68\n"
			 ":104050000AC0F4FF09C0F3FF08C0DAFF07C0D9FFA8\n"
			 ":1040600006C0E2FF05C0E1FF04C0EAFF03C0E9FFAC\n"
			 ":1040700002C0FBFF01C0FAFF11003FEF20F000017A\n"
			 ":0840800042EF20F03DEF20F0BB\n"
			 ":00000001FF\n");
}

static void
fu_firmware_ihex_signed_func (void)
{
	const guint8 *data;
	gboolean ret;
	gsize len;
	g_autofree gchar *filename_shex = NULL;
	g_autoptr(FuFirmware) firmware = fu_ihex_firmware_new ();
	g_autoptr(GBytes) data_file = NULL;
	g_autoptr(GBytes) data_fw = NULL;
	g_autoptr(GBytes) data_sig = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GFile) file_hex = NULL;

	/* load a signed Intel hex32 file */
	filename_shex = g_build_filename (TESTDATADIR_SRC, "firmware.shex", NULL);
	file_hex = g_file_new_for_path (filename_shex);
	data_file = g_file_load_bytes (file_hex, NULL, NULL, &error);
	g_assert_no_error (error);
	g_assert (data_file != NULL);
	ret = fu_firmware_parse (firmware, data_file, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error (error);
	g_assert (ret);
	data_fw = fu_firmware_get_image_by_id_bytes (firmware, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (data_fw);
	g_assert_cmpint (g_bytes_get_size (data_fw), ==, 136);

	/* get the signed image */
	data_sig = fu_firmware_get_image_by_id_bytes (firmware,
						      FU_FIRMWARE_IMAGE_ID_SIGNATURE,
						      &error);
	g_assert_no_error (error);
	g_assert_nonnull (data_sig);
	data = g_bytes_get_data (data_sig, &len);
	g_assert_cmpint (len, ==, 8);
	g_assert (data != NULL);
	g_assert_cmpint (memcmp (data, "deadbeef", 8), ==, 0);
}

static void
fu_firmware_ihex_offset_func (void)
{
	const guint8 *data;
	gboolean ret;
	gsize len;
	g_autofree gchar *str = NULL;
	g_autoptr(FuFirmware) firmware = fu_ihex_firmware_new ();
	g_autoptr(FuFirmware) firmware_verify = fu_ihex_firmware_new ();
	g_autoptr(FuFirmwareImage) img_verify = NULL;
	g_autoptr(FuFirmwareImage) img = NULL;
	g_autoptr(GBytes) data_bin = NULL;
	g_autoptr(GBytes) data_dummy = NULL;
	g_autoptr(GBytes) data_verify = NULL;
	g_autoptr(GError) error = NULL;

	/* add a 4 byte image in high memory */
	data_dummy = g_bytes_new_static ("foo", 4);
	img = fu_firmware_image_new (data_dummy);
	fu_firmware_image_set_addr (img, 0x80000000);
	fu_firmware_add_image (firmware, img);
	data_bin = fu_firmware_write (firmware, &error);
	g_assert_no_error (error);
	g_assert (data_bin != NULL);
	data = g_bytes_get_data (data_bin, &len);
	str = g_strndup ((const gchar *) data, len);
	g_assert_cmpstr (str, ==,
			 ":0200000480007A\n"
			 ":04000000666F6F00B8\n"
			 ":00000001FF\n");

	/* check we can load it too */
	ret = fu_firmware_parse (firmware_verify, data_bin, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error (error);
	g_assert (ret);
	img_verify = fu_firmware_get_image_default (firmware_verify, &error);
	g_assert_no_error (error);
	g_assert (img_verify != NULL);
	g_assert_cmpint (fu_firmware_image_get_addr (img_verify), ==, 0x80000000);
	data_verify = fu_firmware_image_write (img_verify, &error);
	g_assert_no_error (error);
	g_assert (data_verify != NULL);
	g_assert_cmpint (g_bytes_get_size (data_verify), ==, 0x4);
}

static void
fu_firmware_srec_func (void)
{
	gboolean ret;
	g_autofree gchar *filename_srec = NULL;
	g_autofree gchar *filename_ref = NULL;
	g_autoptr(FuFirmware) firmware = fu_srec_firmware_new ();
	g_autoptr(GBytes) data_ref = NULL;
	g_autoptr(GBytes) data_srec = NULL;
	g_autoptr(GBytes) data_bin = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GFile) file_bin = NULL;
	g_autoptr(GFile) file_srec = NULL;

	filename_srec = g_build_filename (TESTDATADIR_SRC, "firmware.srec", NULL);
	file_srec = g_file_new_for_path (filename_srec);
	data_srec = g_file_load_bytes (file_srec, NULL, NULL, &error);
	g_assert_no_error (error);
	g_assert (data_srec != NULL);
	ret = fu_firmware_parse (firmware, data_srec, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error (error);
	g_assert (ret);
	data_bin = fu_firmware_get_image_default_bytes (firmware, &error);
	g_assert_no_error (error);
	g_assert_nonnull (data_bin);
	g_assert_cmpint (g_bytes_get_size (data_bin), ==, 136);

	/* did we match the reference file? */
	filename_ref = g_build_filename (TESTDATADIR_SRC, "firmware.bin", NULL);
	file_bin = g_file_new_for_path (filename_ref);
	data_ref = g_file_load_bytes (file_bin, NULL, NULL, &error);
	g_assert_no_error (error);
	g_assert (data_ref != NULL);
	ret = fu_common_bytes_compare (data_bin, data_ref, &error);
	g_assert_no_error (error);
	g_assert_true (ret);
}

static void
fu_firmware_srec_tokenization_func (void)
{
	FuSrecFirmwareRecord *rcd;
	GPtrArray *records;
	gboolean ret;
	g_autoptr(FuFirmware) firmware = fu_srec_firmware_new ();
	g_autoptr(GBytes) data_srec = NULL;
	g_autoptr(GError) error = NULL;
	const gchar *buf = "S3060000001400E5\r\n"
			   "S31000000002281102000000007F0304002C\r\n"
			   "S306000000145095\r\n"
			   "S70500000000FA\r\n";
	data_srec = g_bytes_new_static (buf, strlen (buf));
	g_assert_no_error (error);
	g_assert (data_srec != NULL);
	ret = fu_firmware_tokenize (firmware, data_srec, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error (error);
	g_assert (ret);

	records = fu_srec_firmware_get_records (FU_SREC_FIRMWARE (firmware));
	g_assert_nonnull (records);
	g_assert_cmpint (records->len, ==, 4);
	rcd = g_ptr_array_index (records, 2);
	g_assert_nonnull (rcd);
	g_assert_cmpint (rcd->ln, ==, 0x3);
	g_assert_cmpint (rcd->kind, ==, 3);
	g_assert_cmpint (rcd->addr, ==, 0x14);
	g_assert_cmpint (rcd->buf->len, ==, 0x1);
	g_assert_cmpint (rcd->buf->data[0], ==, 0x50);
}

static void
fu_firmware_dfu_func (void)
{
	gboolean ret;
	g_autofree gchar *filename_dfu = NULL;
	g_autofree gchar *filename_ref = NULL;
	g_autoptr(FuFirmware) firmware = fu_dfu_firmware_new ();
	g_autoptr(GBytes) data_ref = NULL;
	g_autoptr(GBytes) data_dfu = NULL;
	g_autoptr(GBytes) data_bin = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GFile) file_bin = NULL;
	g_autoptr(GFile) file_dfu = NULL;

	filename_dfu = g_build_filename (TESTDATADIR_SRC, "firmware.dfu", NULL);
	file_dfu = g_file_new_for_path (filename_dfu);
	data_dfu = g_file_load_bytes (file_dfu, NULL, NULL, &error);
	g_assert_no_error (error);
	g_assert (data_dfu != NULL);
	ret = fu_firmware_parse (firmware, data_dfu, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (fu_dfu_firmware_get_vid (FU_DFU_FIRMWARE (firmware)), ==, 0x1234);
	g_assert_cmpint (fu_dfu_firmware_get_pid (FU_DFU_FIRMWARE (firmware)), ==, 0x4321);
	g_assert_cmpint (fu_dfu_firmware_get_release (FU_DFU_FIRMWARE (firmware)), ==, 0xdead);
	data_bin = fu_firmware_get_image_default_bytes (firmware, &error);
	g_assert_no_error (error);
	g_assert_nonnull (data_bin);
	g_assert_cmpint (g_bytes_get_size (data_bin), ==, 136);

	/* did we match the reference file? */
	filename_ref = g_build_filename (TESTDATADIR_SRC, "firmware.bin", NULL);
	file_bin = g_file_new_for_path (filename_ref);
	data_ref = g_file_load_bytes (file_bin, NULL, NULL, &error);
	g_assert_no_error (error);
	g_assert (data_ref != NULL);
	ret = fu_common_bytes_compare (data_bin, data_ref, &error);
	g_assert_no_error (error);
	g_assert_true (ret);
}

static void
fu_firmware_func (void)
{
	g_autoptr(FuFirmware) firmware = fu_firmware_new ();
	g_autoptr(FuFirmwareImage) img1 = fu_firmware_image_new (NULL);
	g_autoptr(FuFirmwareImage) img2 = fu_firmware_image_new (NULL);
	g_autoptr(FuFirmwareImage) img_id = NULL;
	g_autoptr(FuFirmwareImage) img_idx = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *str = NULL;

	fu_firmware_image_set_addr (img1, 0x200);
	fu_firmware_image_set_idx (img1, 13);
	fu_firmware_image_set_id (img1, "primary");
	fu_firmware_add_image (firmware, img1);
	fu_firmware_image_set_addr (img2, 0x400);
	fu_firmware_image_set_idx (img2, 23);
	fu_firmware_image_set_id (img2, "secondary");
	fu_firmware_add_image (firmware, img2);

	img_id = fu_firmware_get_image_by_id (firmware, "NotGoingToExist", &error);
	g_assert_error (error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_null (img_id);
	g_clear_error (&error);
	img_id = fu_firmware_get_image_by_id (firmware, "primary", &error);
	g_assert_no_error (error);
	g_assert_nonnull (img_id);
	g_assert_cmpint (fu_firmware_image_get_addr (img_id), ==, 0x200);
	g_assert_cmpint (fu_firmware_image_get_idx (img_id), ==, 13);
	g_assert_cmpstr (fu_firmware_image_get_id (img_id), ==, "primary");

	img_idx = fu_firmware_get_image_by_idx (firmware, 123456, &error);
	g_assert_error (error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_null (img_idx);
	g_clear_error (&error);
	img_idx = fu_firmware_get_image_by_idx (firmware, 23, &error);
	g_assert_no_error (error);
	g_assert_nonnull (img_idx);
	g_assert_cmpint (fu_firmware_image_get_addr (img_idx), ==, 0x400);
	g_assert_cmpint (fu_firmware_image_get_idx (img_idx), ==, 23);
	g_assert_cmpstr (fu_firmware_image_get_id (img_idx), ==, "secondary");

	str = fu_firmware_to_string (firmware);
	g_assert_cmpstr (str, ==, "FuFirmware:\n"
				  "  FuFirmwareImage:\n"
				  "  ID:                    primary\n"
				  "  Index:                 0xd\n"
				  "  Address:               0x200\n"
				  "  FuFirmwareImage:\n"
				  "  ID:                    secondary\n"
				  "  Index:                 0x17\n"
				  "  Address:               0x400\n");
}

int
main (int argc, char **argv)
{
	g_test_init (&argc, &argv, NULL);

	/* only critical and error are fatal */
	g_log_set_fatal_mask (NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);
	g_setenv ("G_MESSAGES_DEBUG", "all", TRUE);
	g_setenv ("FWUPD_DATADIR", TESTDATADIR_SRC, TRUE);
	g_setenv ("FWUPD_PLUGINDIR", TESTDATADIR_SRC, TRUE);
	g_setenv ("FWUPD_SYSCONFDIR", TESTDATADIR_SRC, TRUE);
	g_setenv ("FWUPD_SYSFSFWDIR", TESTDATADIR_SRC, TRUE);
	g_setenv ("FWUPD_OFFLINE_TRIGGER", "/tmp/fwupd-self-test/system-update", TRUE);
	g_setenv ("FWUPD_LOCALSTATEDIR", "/tmp/fwupd-self-test/var", TRUE);

	g_test_add_func ("/fwupd/plugin{delay}", fu_plugin_delay_func);
	g_test_add_func ("/fwupd/plugin{quirks}", fu_plugin_quirks_func);
	g_test_add_func ("/fwupd/plugin{quirks-performance}", fu_plugin_quirks_performance_func);
	g_test_add_func ("/fwupd/plugin{quirks-device}", fu_plugin_quirks_device_func);
	g_test_add_func ("/fwupd/chunk", fu_chunk_func);
	g_test_add_func ("/fwupd/common{string-append-kv}", fu_common_string_append_kv_func);
	g_test_add_func ("/fwupd/common{version-guess-format}", fu_common_version_guess_format_func);
	g_test_add_func ("/fwupd/common{version}", fu_common_version_func);
	g_test_add_func ("/fwupd/common{vercmp}", fu_common_vercmp_func);
	g_test_add_func ("/fwupd/common{strstrip}", fu_common_strstrip_func);
	g_test_add_func ("/fwupd/common{endian}", fu_common_endian_func);
	g_test_add_func ("/fwupd/common{cab-success}", fu_common_store_cab_func);
	g_test_add_func ("/fwupd/common{cab-success-unsigned}", fu_common_store_cab_unsigned_func);
	g_test_add_func ("/fwupd/common{cab-success-folder}", fu_common_store_cab_folder_func);
	g_test_add_func ("/fwupd/common{cab-error-no-metadata}", fu_common_store_cab_error_no_metadata_func);
	g_test_add_func ("/fwupd/common{cab-error-wrong-size}", fu_common_store_cab_error_wrong_size_func);
	g_test_add_func ("/fwupd/common{cab-error-wrong-checksum}", fu_common_store_cab_error_wrong_checksum_func);
	g_test_add_func ("/fwupd/common{cab-error-missing-file}", fu_common_store_cab_error_missing_file_func);
	g_test_add_func ("/fwupd/common{cab-error-size}", fu_common_store_cab_error_size_func);
	g_test_add_func ("/fwupd/common{spawn)", fu_common_spawn_func);
	g_test_add_func ("/fwupd/common{spawn-timeout)", fu_common_spawn_timeout_func);
	g_test_add_func ("/fwupd/common{firmware-builder}", fu_common_firmware_builder_func);
	g_test_add_func ("/fwupd/common{kernel-lockdown}", fu_common_kernel_lockdown_func);
	g_test_add_func ("/fwupd/hwids", fu_hwids_func);
	g_test_add_func ("/fwupd/smbios", fu_smbios_func);
	g_test_add_func ("/fwupd/smbios3", fu_smbios3_func);
	g_test_add_func ("/fwupd/firmware", fu_firmware_func);
	g_test_add_func ("/fwupd/firmware{ihex}", fu_firmware_ihex_func);
	g_test_add_func ("/fwupd/firmware{ihex-offset}", fu_firmware_ihex_offset_func);
	g_test_add_func ("/fwupd/firmware{ihex-signed}", fu_firmware_ihex_signed_func);
	g_test_add_func ("/fwupd/firmware{srec-tokenization}", fu_firmware_srec_tokenization_func);
	g_test_add_func ("/fwupd/firmware{srec}", fu_firmware_srec_func);
	g_test_add_func ("/fwupd/firmware{dfu}", fu_firmware_dfu_func);
	g_test_add_func ("/fwupd/archive{invalid}", fu_archive_invalid_func);
	g_test_add_func ("/fwupd/archive{cab}", fu_archive_cab_func);
	g_test_add_func ("/fwupd/device{incorporate}", fu_device_incorporate_func);
	if (g_test_slow ())
		g_test_add_func ("/fwupd/device{poll}", fu_device_poll_func);
	g_test_add_func ("/fwupd/device-locker{success}", fu_device_locker_func);
	g_test_add_func ("/fwupd/device-locker{fail}", fu_device_locker_fail_func);
	g_test_add_func ("/fwupd/device{metadata}", fu_device_metadata_func);
	g_test_add_func ("/fwupd/device{open-refcount}", fu_device_open_refcount_func);
	g_test_add_func ("/fwupd/device{version-format}", fu_device_version_format_func);
	return g_test_run ();
}
