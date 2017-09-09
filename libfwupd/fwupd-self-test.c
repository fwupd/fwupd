/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
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

#include <glib-object.h>
#include <fnmatch.h>

#include "fwupd-client.h"
#include "fwupd-enums.h"
#include "fwupd-error.h"
#include "fwupd-remote-private.h"
#include "fwupd-result.h"

static gboolean
as_test_compare_lines (const gchar *txt1, const gchar *txt2, GError **error)
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

static void
fwupd_enums_func (void)
{
	const gchar *tmp;
	guint64 i;

	/* enums */
	for (i = 0; i < FWUPD_ERROR_LAST; i++) {
		tmp = fwupd_error_to_string (i);
		g_assert_cmpstr (tmp, !=, NULL);
		g_assert_cmpint (fwupd_error_from_string (tmp), ==, i);
	}
	for (i = 0; i < FWUPD_STATUS_LAST; i++) {
		tmp = fwupd_status_to_string (i);
		g_assert_cmpstr (tmp, !=, NULL);
		g_assert_cmpint (fwupd_status_from_string (tmp), ==, i);
	}
	for (i = 0; i < FWUPD_UPDATE_STATE_LAST; i++) {
		tmp = fwupd_update_state_to_string (i);
		g_assert_cmpstr (tmp, !=, NULL);
		g_assert_cmpint (fwupd_update_state_from_string (tmp), ==, i);
	}
	for (i = 0; i < FWUPD_TRUST_FLAG_LAST; i++) {
		tmp = fwupd_trust_flag_to_string (i);
		g_assert_cmpstr (tmp, !=, NULL);
		g_assert_cmpint (fwupd_trust_flag_from_string (tmp), ==, i);
	}

	/* bitfield */
	for (i = 1; i < FWUPD_DEVICE_FLAG_UNKNOWN; i *= 2) {
		tmp = fwupd_device_flag_to_string (i);
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
	g_assert_cmpstr (fwupd_remote_get_filename (remote), ==, "lvfs-firmware.xml.gz");
	g_assert_cmpstr (fwupd_remote_get_filename_asc (remote), ==, "lvfs-firmware.xml.gz.asc");
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
	g_assert_cmpstr (fwupd_remote_get_metadata_uri (remote), ==,
			 "https://s3.amazonaws.com/lvfsbucket/downloads/firmware.xml.gz");
	g_assert_cmpstr (fwupd_remote_get_metadata_uri_sig (remote), ==,
			 "https://s3.amazonaws.com/lvfsbucket/downloads/firmware.xml.gz.asc");
	firmware_uri = fwupd_remote_build_firmware_uri (remote, "http://bbc.co.uk/firmware.cab", &error);
	g_assert_no_error (error);
	g_assert_cmpstr (firmware_uri, ==, "https://my.fancy.cdn/firmware.cab");
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
	g_assert_cmpstr (fwupd_remote_get_title (remote), ==, "Core");
	g_assert_cmpstr (fwupd_remote_get_filename (remote), ==, NULL);
	g_assert_cmpstr (fwupd_remote_get_filename_asc (remote), ==, NULL);
	g_assert_cmpstr (fwupd_remote_get_filename_cache (remote), ==, "@datadir@/fwupd/remotes.d/fwupd/metadata.xml");
	g_assert_cmpstr (fwupd_remote_get_filename_cache_sig (remote), ==, NULL);
}

static void
fwupd_result_func (void)
{
	FwupdDevice *dev;
	FwupdRelease *rel;
	gboolean ret;
	g_autofree gchar *str = NULL;
	g_autoptr(FwupdResult) result = NULL;
	g_autoptr(GError) error = NULL;

	/* create dummy object */
	result = fwupd_result_new ();
	dev = fwupd_result_get_device (result);
	fwupd_device_add_checksum (dev, "beefdead");
	fwupd_device_set_created (dev, 1);
	fwupd_device_set_flags (dev, FWUPD_DEVICE_FLAG_UPDATABLE);
	fwupd_device_set_id (dev, "USB:foo");
	fwupd_device_set_modified (dev, 60 * 60 * 24);
	fwupd_device_set_name (dev, "ColorHug2");
	fwupd_device_add_guid (dev, "2082b5e0-7a64-478a-b1b2-e3404fab6dad");
	fwupd_device_add_guid (dev, "00000000-0000-0000-0000-000000000000");
	fwupd_device_add_flag (dev, FWUPD_DEVICE_FLAG_REQUIRE_AC);
	fwupd_result_set_update_trust_flags (result, FWUPD_TRUST_FLAG_PAYLOAD);

	rel = fwupd_result_get_release (result);
	fwupd_release_add_checksum (rel, "deadbeef");
	fwupd_release_set_description (rel, "<p>Hi there!</p>");
	fwupd_release_set_filename (rel, "firmware.bin");
	fwupd_release_set_appstream_id (rel, "org.dave.ColorHug.firmware");
	fwupd_release_set_size (rel, 1024);
	fwupd_release_set_uri (rel, "http://foo.com");
	fwupd_release_set_version (rel, "1.2.3");
	str = fwupd_result_to_string (result);
	g_print ("\n%s", str);

	/* check GUIDs */
	g_assert (fwupd_device_has_guid (dev, "2082b5e0-7a64-478a-b1b2-e3404fab6dad"));
	g_assert (fwupd_device_has_guid (dev, "00000000-0000-0000-0000-000000000000"));
	g_assert (!fwupd_device_has_guid (dev, "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"));

	ret = as_test_compare_lines (str,
		"ColorHug2\n"
		"  DeviceID:             USB:foo\n"
		"  Guid:                 2082b5e0-7a64-478a-b1b2-e3404fab6dad\n"
		"  Guid:                 00000000-0000-0000-0000-000000000000\n"
		"  Flags:                updatable|require-ac\n"
		"  FirmwareHash:         SHA1(beefdead)\n"
		"  Created:              1970-01-01\n"
		"  Modified:             1970-01-02\n"
		"  AppstreamId:          org.dave.ColorHug.firmware\n"
		"  UpdateDescription:    <p>Hi there!</p>\n"
		"  UpdateVersion:        1.2.3\n"
		"  FilenameCab:          firmware.bin\n"
		"  UpdateHash:           SHA1(deadbeef)\n"
		"  Size:                 1.0 kB\n"
		"  UpdateUri:            http://foo.com\n"
		"  Trusted:              payload\n", &error);
	g_assert_no_error (error);
	g_assert (ret);
}

static void
fwupd_client_devices_func (void)
{
	FwupdDevice *dev;
	g_autoptr(FwupdClient) client = NULL;
	g_autoptr(GPtrArray) array = NULL;
	g_autoptr(GError) error = NULL;

	client = fwupd_client_new ();
	array = fwupd_client_get_devices_simple (client, NULL, &error);
	if (array == NULL &&
	    g_error_matches (error, FWUPD_ERROR, FWUPD_ERROR_NOTHING_TO_DO))
		return;
	if (array == NULL &&
	    g_error_matches (error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED))
		return;
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
	g_autoptr(FwupdClient) client = NULL;
	g_autoptr(FwupdRemote) remote2 = NULL;
	g_autoptr(FwupdRemote) remote3 = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) array = NULL;

	g_setenv ("FU_SELF_TEST_REMOTES_DIR", FU_SELF_TEST_REMOTES_DIR, TRUE);

	client = fwupd_client_new ();
	array = fwupd_client_get_remotes (client, NULL, &error);
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

static void
fwupd_client_updates_func (void)
{
	FwupdDevice *dev;
	FwupdResult *res;
	g_autoptr(FwupdClient) client = NULL;
	g_autoptr(GPtrArray) array = NULL;
	g_autoptr(GError) error = NULL;

	client = fwupd_client_new ();
	array = fwupd_client_get_updates (client, NULL, &error);
	if (array == NULL &&
	    g_error_matches (error, FWUPD_ERROR, FWUPD_ERROR_NOTHING_TO_DO))
		return;
	if (array == NULL &&
	    g_error_matches (error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED))
		return;
	g_assert_no_error (error);
	g_assert (array != NULL);
	g_assert_cmpint (array->len, >, 0);

	/* check device */
	res = g_ptr_array_index (array, 0);
	g_assert (FWUPD_IS_RESULT (res));
	dev = fwupd_result_get_device (res);
	g_assert_cmpstr (fwupd_device_get_guid_default (dev), !=, NULL);
	g_assert_cmpstr (fwupd_device_get_id (dev), !=, NULL);
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

int
main (int argc, char **argv)
{
	g_test_init (&argc, &argv, NULL);

	/* only critical and error are fatal */
	g_log_set_fatal_mask (NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);
	g_setenv ("G_MESSAGES_DEBUG", "all", TRUE);

	/* tests go here */
	g_test_add_func ("/fwupd/enums", fwupd_enums_func);
	g_test_add_func ("/fwupd/result", fwupd_result_func);
	g_test_add_func ("/fwupd/remote{download}", fwupd_remote_download_func);
	g_test_add_func ("/fwupd/remote{base-uri}", fwupd_remote_baseuri_func);
	g_test_add_func ("/fwupd/remote{local}", fwupd_remote_local_func);
	if (fwupd_has_system_bus ()) {
		g_test_add_func ("/fwupd/client{remotes}", fwupd_client_remotes_func);
		g_test_add_func ("/fwupd/client{devices}", fwupd_client_devices_func);
		g_test_add_func ("/fwupd/client{updates}", fwupd_client_updates_func);
	}
	return g_test_run ();
}
