/*
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <glib-object.h>
#include <fnmatch.h>

#include "fwupd-client.h"
#include "fwupd-common.h"
#include "fwupd-enums.h"
#include "fwupd-error.h"
#include "fwupd-release-private.h"
#include "fwupd-remote-private.h"

static gboolean
fu_test_compare_lines (const gchar *txt1, const gchar *txt2, GError **error)
{
	g_autofree gchar *output = NULL;

	/* exactly the same */
	if (g_strcmp0 (txt1, txt2) == 0)
		return TRUE;

	/* matches a pattern */
	if (fnmatch (txt2, txt1, FNM_NOESCAPE) == 0)
		return TRUE;

	/* save temp files and diff them */
	if (!g_file_set_contents ("/tmp/a", txt1, -1, error))
		return FALSE;
	if (!g_file_set_contents ("/tmp/b", txt2, -1, error))
		return FALSE;
	if (!g_spawn_command_line_sync ("diff -urNp /tmp/b /tmp/a",
					&output, NULL, NULL, error))
		return FALSE;

	/* just output the diff */
	g_set_error_literal (error, 1, 0, output);
	return FALSE;
}

/* https://gitlab.gnome.org/GNOME/glib/issues/225 */
static guint
_g_string_replace (GString *string, const gchar *search, const gchar *replace)
{
	gchar *tmp;
	guint count = 0;
	gsize search_idx = 0;
	gsize replace_len;
	gsize search_len;

	g_return_val_if_fail (string != NULL, 0);
	g_return_val_if_fail (search != NULL, 0);
	g_return_val_if_fail (replace != NULL, 0);

	/* nothing to do */
	if (string->len == 0)
		return 0;

	search_len = strlen (search);
	replace_len = strlen (replace);

	do {
		tmp = g_strstr_len (string->str + search_idx, -1, search);
		if (tmp == NULL)
			break;

		/* advance the counter in case @replace contains @search */
		search_idx = (gsize) (tmp - string->str);

		/* reallocate the string if required */
		if (search_len > replace_len) {
			g_string_erase (string,
					(gssize) search_idx,
					(gssize) (search_len - replace_len));
			memcpy (tmp, replace, replace_len);
		} else if (search_len < replace_len) {
			g_string_insert_len (string,
					     (gssize) search_idx,
					     replace,
					     (gssize) (replace_len - search_len));
			/* we have to treat this specially as it could have
			 * been reallocated when the insertion happened */
			memcpy (string->str + search_idx, replace, replace_len);
		} else {
			/* just memcmp in the new string */
			memcpy (tmp, replace, replace_len);
		}
		search_idx += replace_len;
		count++;
	} while (TRUE);

	return count;
}

static void
fwupd_enums_func (void)
{
	/* enums */
	for (guint i = 0; i < FWUPD_ERROR_LAST; i++) {
		const gchar *tmp = fwupd_error_to_string (i);
		g_assert_cmpstr (tmp, !=, NULL);
		g_assert_cmpint (fwupd_error_from_string (tmp), ==, i);
	}
	for (guint i = 0; i < FWUPD_STATUS_LAST; i++) {
		const gchar *tmp = fwupd_status_to_string (i);
		g_assert_cmpstr (tmp, !=, NULL);
		g_assert_cmpint (fwupd_status_from_string (tmp), ==, i);
	}
	for (guint i = 0; i < FWUPD_UPDATE_STATE_LAST; i++) {
		const gchar *tmp = fwupd_update_state_to_string (i);
		g_assert_cmpstr (tmp, !=, NULL);
		g_assert_cmpint (fwupd_update_state_from_string (tmp), ==, i);
	}
	for (guint i = 0; i < FWUPD_TRUST_FLAG_LAST; i++) {
		const gchar *tmp = fwupd_trust_flag_to_string (i);
		g_assert_cmpstr (tmp, !=, NULL);
		g_assert_cmpint (fwupd_trust_flag_from_string (tmp), ==, i);
	}

	/* bitfield */
	for (guint64 i = 1; i < FWUPD_DEVICE_FLAG_UNKNOWN; i *= 2) {
		const gchar *tmp = fwupd_device_flag_to_string (i);
		if (tmp == NULL)
			break;
		g_assert_cmpint (fwupd_device_flag_from_string (tmp), ==, i);
	}
}

static void
fwupd_remote_download_func (void)
{
	gboolean ret;
	g_autofree gchar *fn = NULL;
	g_autoptr(FwupdRemote) remote = NULL;
	g_autoptr(GError) error = NULL;

	remote = fwupd_remote_new ();
	fn = g_build_filename (FU_SELF_TEST_REMOTES_DIR, "remotes.d", "lvfs.conf", NULL);
	ret = fwupd_remote_load_from_filename (remote, fn, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (fwupd_remote_get_kind (remote), ==, FWUPD_REMOTE_KIND_DOWNLOAD);
	g_assert_cmpint (fwupd_remote_get_keyring_kind (remote), ==, FWUPD_KEYRING_KIND_GPG);
	g_assert_cmpint (fwupd_remote_get_priority (remote), ==, 0);
	g_assert (fwupd_remote_get_enabled (remote));
	g_assert (fwupd_remote_get_metadata_uri (remote) != NULL);
	g_assert (fwupd_remote_get_metadata_uri_sig (remote) != NULL);
	g_assert_cmpstr (fwupd_remote_get_title (remote), ==, "Linux Vendor Firmware Service");
	g_assert_cmpstr (fwupd_remote_get_report_uri (remote), ==, "https://fwupd.org/lvfs/firmware/report");
	g_assert_cmpstr (fwupd_remote_get_filename_cache (remote), ==,
			 LOCALSTATEDIR "/lib/fwupd/remotes.d/lvfs/metadata.xml.gz");
	g_assert_cmpstr (fwupd_remote_get_filename_cache_sig (remote), ==,
			 LOCALSTATEDIR "/lib/fwupd/remotes.d/lvfs/metadata.xml.gz.asc");
}

/* verify we used the FirmwareBaseURI just for firmware */
static void
fwupd_remote_baseuri_func (void)
{
	gboolean ret;
	g_autofree gchar *firmware_uri = NULL;
	g_autofree gchar *fn = NULL;
	g_autoptr(FwupdRemote) remote = NULL;
	g_autoptr(GError) error = NULL;

	remote = fwupd_remote_new ();
	fn = g_build_filename (TESTDATADIR, "tests", "firmware-base-uri.conf", NULL);
	ret = fwupd_remote_load_from_filename (remote, fn, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (fwupd_remote_get_kind (remote), ==, FWUPD_REMOTE_KIND_DOWNLOAD);
	g_assert_cmpint (fwupd_remote_get_keyring_kind (remote), ==, FWUPD_KEYRING_KIND_GPG);
	g_assert_cmpint (fwupd_remote_get_priority (remote), ==, 0);
	g_assert (fwupd_remote_get_enabled (remote));
	g_assert_cmpstr (fwupd_remote_get_checksum (remote), ==, NULL);
	g_assert_cmpstr (fwupd_remote_get_metadata_uri (remote), ==,
			 "https://s3.amazonaws.com/lvfsbucket/downloads/firmware.xml.gz");
	g_assert_cmpstr (fwupd_remote_get_metadata_uri_sig (remote), ==,
			 "https://s3.amazonaws.com/lvfsbucket/downloads/firmware.xml.gz.asc");
	firmware_uri = fwupd_remote_build_firmware_uri (remote, "http://bbc.co.uk/firmware.cab", &error);
	g_assert_no_error (error);
	g_assert_cmpstr (firmware_uri, ==, "https://my.fancy.cdn/firmware.cab");
}

/* verify we used the metadata path for firmware */
static void
fwupd_remote_nopath_func (void)
{
	gboolean ret;
	g_autofree gchar *firmware_uri = NULL;
	g_autofree gchar *fn = NULL;
	g_autoptr(FwupdRemote) remote = NULL;
	g_autoptr(GError) error = NULL;

	remote = fwupd_remote_new ();
	fn = g_build_filename (TESTDATADIR, "tests", "firmware-nopath.conf", NULL);
	ret = fwupd_remote_load_from_filename (remote, fn, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (fwupd_remote_get_kind (remote), ==, FWUPD_REMOTE_KIND_DOWNLOAD);
	g_assert_cmpint (fwupd_remote_get_keyring_kind (remote), ==, FWUPD_KEYRING_KIND_GPG);
	g_assert_cmpint (fwupd_remote_get_priority (remote), ==, 0);
	g_assert (fwupd_remote_get_enabled (remote));
	g_assert_cmpstr (fwupd_remote_get_checksum (remote), ==, NULL);
	g_assert_cmpstr (fwupd_remote_get_metadata_uri (remote), ==,
			 "https://s3.amazonaws.com/lvfsbucket/downloads/firmware.xml.gz");
	g_assert_cmpstr (fwupd_remote_get_metadata_uri_sig (remote), ==,
			 "https://s3.amazonaws.com/lvfsbucket/downloads/firmware.xml.gz.asc");
	firmware_uri = fwupd_remote_build_firmware_uri (remote, "firmware.cab", &error);
	g_assert_no_error (error);
	g_assert_cmpstr (firmware_uri, ==, "https://s3.amazonaws.com/lvfsbucket/downloads/firmware.cab");
}

static void
fwupd_remote_local_func (void)
{
	gboolean ret;
	g_autofree gchar *fn = NULL;
	g_autoptr(FwupdRemote) remote = NULL;
	g_autoptr(GError) error = NULL;

	remote = fwupd_remote_new ();
	fn = g_build_filename (FU_SELF_TEST_REMOTES_DIR, "remotes.d", "fwupd.conf", NULL);
	ret = fwupd_remote_load_from_filename (remote, fn, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (fwupd_remote_get_kind (remote), ==, FWUPD_REMOTE_KIND_LOCAL);
	g_assert_cmpint (fwupd_remote_get_keyring_kind (remote), ==, FWUPD_KEYRING_KIND_NONE);
	g_assert (fwupd_remote_get_enabled (remote));
	g_assert (fwupd_remote_get_metadata_uri (remote) == NULL);
	g_assert (fwupd_remote_get_metadata_uri_sig (remote) == NULL);
	g_assert (fwupd_remote_get_report_uri (remote) == NULL);
	g_assert_cmpstr (fwupd_remote_get_title (remote), ==, "Core");
	g_assert_cmpstr (fwupd_remote_get_filename_cache (remote), ==, "@datadir@/fwupd/remotes.d/fwupd/metadata.xml");
	g_assert_cmpstr (fwupd_remote_get_filename_cache_sig (remote), ==, NULL);
	g_assert_cmpstr (fwupd_remote_get_checksum (remote), ==, NULL);
}

static void
fwupd_release_func (void)
{
	g_autoptr(FwupdRelease) release1 = NULL;
	g_autoptr(FwupdRelease) release2 = NULL;
	g_autoptr(GVariant) data = NULL;

	release1 = fwupd_release_new ();
	fwupd_release_add_metadata_item (release1, "foo", "bar");
	fwupd_release_add_metadata_item (release1, "baz", "bam");
	data = fwupd_release_to_variant (release1);
	release2 = fwupd_release_from_variant (data);
	g_assert_cmpstr (fwupd_release_get_metadata_item (release2, "foo"), ==, "bar");
	g_assert_cmpstr (fwupd_release_get_metadata_item (release2, "baz"), ==, "bam");
}

static void
fwupd_device_func (void)
{
	gboolean ret;
	g_autofree gchar *str = NULL;
	g_autoptr(FwupdDevice) dev = NULL;
	g_autoptr(FwupdRelease) rel = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GString) str_ascii = NULL;

	/* create dummy object */
	dev = fwupd_device_new ();
	fwupd_device_add_checksum (dev, "beefdead");
	fwupd_device_set_created (dev, 1);
	fwupd_device_set_flags (dev, FWUPD_DEVICE_FLAG_UPDATABLE);
	fwupd_device_set_id (dev, "USB:foo");
	fwupd_device_set_modified (dev, 60 * 60 * 24);
	fwupd_device_set_name (dev, "ColorHug2");
	fwupd_device_add_guid (dev, "2082b5e0-7a64-478a-b1b2-e3404fab6dad");
	fwupd_device_add_guid (dev, "00000000-0000-0000-0000-000000000000");
	fwupd_device_add_icon (dev, "input-gaming");
	fwupd_device_add_icon (dev, "input-mouse");
	fwupd_device_add_flag (dev, FWUPD_DEVICE_FLAG_REQUIRE_AC);
	rel = fwupd_release_new ();
	fwupd_release_set_trust_flags (rel, FWUPD_TRUST_FLAG_PAYLOAD);
	fwupd_release_add_checksum (rel, "deadbeef");
	fwupd_release_set_description (rel, "<p>Hi there!</p>");
	fwupd_release_set_filename (rel, "firmware.bin");
	fwupd_release_set_appstream_id (rel, "org.dave.ColorHug.firmware");
	fwupd_release_set_size (rel, 1024);
	fwupd_release_set_uri (rel, "http://foo.com");
	fwupd_release_set_version (rel, "1.2.3");
	fwupd_device_add_release (dev, rel);
	str = fwupd_device_to_string (dev);
	g_print ("\n%s", str);

	/* check GUIDs */
	g_assert (fwupd_device_has_guid (dev, "2082b5e0-7a64-478a-b1b2-e3404fab6dad"));
	g_assert (fwupd_device_has_guid (dev, "00000000-0000-0000-0000-000000000000"));
	g_assert (!fwupd_device_has_guid (dev, "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"));

	/* convert the new non-breaking space back into a normal space:
	 * https://gitlab.gnome.org/GNOME/glib/commit/76af5dabb4a25956a6c41a75c0c7feeee74496da */
	str_ascii = g_string_new (str);
	_g_string_replace (str_ascii, " ", " ");
	ret = fu_test_compare_lines (str_ascii->str,
		"ColorHug2\n"
		"  DeviceId:             USB:foo\n"
		"  Guid:                 2082b5e0-7a64-478a-b1b2-e3404fab6dad\n"
		"  Guid:                 00000000-0000-0000-0000-000000000000\n"
		"  Flags:                updatable|require-ac\n"
		"  Checksum:             SHA1(beefdead)\n"
		"  Icon:                 input-gaming,input-mouse\n"
		"  Created:              1970-01-01\n"
		"  Modified:             1970-01-02\n"
		"  \n"
		"  [Release]\n"
		"  AppstreamId:          org.dave.ColorHug.firmware\n"
		"  Description:          <p>Hi there!</p>\n"
		"  Version:              1.2.3\n"
		"  Filename:             firmware.bin\n"
		"  Checksum:             SHA1(deadbeef)\n"
		"  Size:                 1.0 kB\n"
		"  Uri:                  http://foo.com\n"
		"  TrustFlags:           payload\n", &error);
	g_assert_no_error (error);
	g_assert (ret);
}

static void
fwupd_client_devices_func (void)
{
	FwupdDevice *dev;
	gboolean ret;
	g_autoptr(FwupdClient) client = NULL;
	g_autoptr(GPtrArray) array = NULL;
	g_autoptr(GError) error = NULL;

	client = fwupd_client_new ();

	/* only run if running fwupd is new enough */
	ret = fwupd_client_connect (client, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (ret);
	if (fwupd_client_get_daemon_version (client) == NULL) {
		g_test_skip ("no enabled fwupd daemon");
		return;
	}
	if (!g_str_has_prefix (fwupd_client_get_daemon_version (client), "1.")) {
		g_test_skip ("running fwupd is too old");
		return;
	}

	array = fwupd_client_get_devices (client, NULL, &error);
	if (array == NULL &&
	    g_error_matches (error, FWUPD_ERROR, FWUPD_ERROR_NOTHING_TO_DO)) {
		g_test_skip ("no available fwupd devices");
		return;
	}
	if (array == NULL &&
	    g_error_matches (error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED)) {
		g_test_skip ("no available fwupd daemon");
		return;
	}
	g_assert_no_error (error);
	g_assert (array != NULL);
	g_assert_cmpint (array->len, >, 0);

	/* check device */
	dev = g_ptr_array_index (array, 0);
	g_assert (FWUPD_IS_DEVICE (dev));
	g_assert_cmpstr (fwupd_device_get_guid_default (dev), !=, NULL);
	g_assert_cmpstr (fwupd_device_get_id (dev), !=, NULL);
}

static void
fwupd_client_remotes_func (void)
{
	gboolean ret;
	g_autoptr(FwupdClient) client = NULL;
	g_autoptr(FwupdRemote) remote2 = NULL;
	g_autoptr(FwupdRemote) remote3 = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) array = NULL;

	g_setenv ("FU_SELF_TEST_REMOTES_DIR", FU_SELF_TEST_REMOTES_DIR, TRUE);

	client = fwupd_client_new ();

	/* only run if running fwupd is new enough */
	ret = fwupd_client_connect (client, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (ret);
	if (fwupd_client_get_daemon_version (client) == NULL) {
		g_test_skip ("no enabled fwupd daemon");
		return;
	}
	if (!g_str_has_prefix (fwupd_client_get_daemon_version (client), "1.")) {
		g_test_skip ("running fwupd is too old");
		return;
	}

	array = fwupd_client_get_remotes (client, NULL, &error);
	if (array == NULL &&
	    g_error_matches (error, FWUPD_ERROR, FWUPD_ERROR_NOTHING_TO_DO)) {
		g_test_skip ("no available fwupd remotes");
		return;
	}
	if (array == NULL &&
	    g_error_matches (error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED)) {
		g_test_skip ("no available fwupd daemon");
		return;
	}
	g_assert_no_error (error);
	g_assert (array != NULL);
	g_assert_cmpint (array->len, >, 0);

	/* check we can find the right thing */
	remote2 = fwupd_client_get_remote_by_id (client, "lvfs", NULL, &error);
	g_assert_no_error (error);
	g_assert (remote2 != NULL);
	g_assert_cmpstr (fwupd_remote_get_id (remote2), ==, "lvfs");
	g_assert (fwupd_remote_get_enabled (remote2));
	g_assert (fwupd_remote_get_metadata_uri (remote2) != NULL);

	/* check we set an error when unfound */
	remote3 = fwupd_client_get_remote_by_id (client, "XXXX", NULL, &error);
	g_assert_error (error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert (remote3 == NULL);
}

static gboolean
fwupd_has_system_bus (void)
{
	g_autoptr(GDBusConnection) conn = NULL;
	conn = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, NULL);
	if (conn != NULL)
		return TRUE;
	g_debug ("D-Bus system bus unavailable, skipping tests.");
	return FALSE;
}

static void
fwupd_common_machine_hash_func (void)
{
	g_autofree gchar *mhash1 = NULL;
	g_autofree gchar *mhash2 = NULL;
	g_autoptr(GError) error = NULL;

	if (!g_file_test ("/etc/machine-id", G_FILE_TEST_EXISTS)) {
		g_test_skip ("Missing /etc/machine-id");
		return;
	}

	mhash1 = fwupd_build_machine_id ("salt1", &error);
	g_assert_no_error (error);
	g_assert_cmpstr (mhash1, !=, NULL);
	mhash2 = fwupd_build_machine_id ("salt2", &error);
	g_assert_no_error (error);
	g_assert_cmpstr (mhash2, !=, NULL);
	g_assert_cmpstr (mhash2, !=, mhash1);
}

int
main (int argc, char **argv)
{
	g_test_init (&argc, &argv, NULL);

	/* only critical and error are fatal */
	g_log_set_fatal_mask (NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);
	g_setenv ("G_MESSAGES_DEBUG", "all", TRUE);

	/* tests go here */
	g_test_add_func ("/fwupd/enums", fwupd_enums_func);
	g_test_add_func ("/fwupd/common{machine-hash}", fwupd_common_machine_hash_func);
	g_test_add_func ("/fwupd/release", fwupd_release_func);
	g_test_add_func ("/fwupd/device", fwupd_device_func);
	g_test_add_func ("/fwupd/remote{download}", fwupd_remote_download_func);
	g_test_add_func ("/fwupd/remote{base-uri}", fwupd_remote_baseuri_func);
	g_test_add_func ("/fwupd/remote{no-path}", fwupd_remote_nopath_func);
	g_test_add_func ("/fwupd/remote{local}", fwupd_remote_local_func);
	if (fwupd_has_system_bus ()) {
		g_test_add_func ("/fwupd/client{remotes}", fwupd_client_remotes_func);
		g_test_add_func ("/fwupd/client{devices}", fwupd_client_devices_func);
	}
	return g_test_run ();
}
