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
fwupd_result_func (void)
{
	gboolean ret;
	g_autofree gchar *str = NULL;
	g_autoptr(FwupdResult) result = NULL;
	g_autoptr(GError) error = NULL;

	/* create dummy object */
	result = fwupd_result_new ();
	fwupd_result_set_device_checksum (result, "beefdead");
	fwupd_result_set_device_checksum_kind (result, G_CHECKSUM_SHA256);
	fwupd_result_set_device_created (result, 1);
	fwupd_result_set_device_flags (result, FWUPD_DEVICE_FLAG_ALLOW_OFFLINE);
	fwupd_result_set_device_id (result, "USB:foo");
	fwupd_result_set_device_modified (result, 60 * 60 * 24);
	fwupd_result_set_device_name (result, "ColorHug2");
	fwupd_result_add_guid (result, "2082b5e0-7a64-478a-b1b2-e3404fab6dad");
	fwupd_result_add_guid (result, "00000000-0000-0000-0000-000000000000");
	fwupd_result_set_update_checksum (result, "deadbeef");
	fwupd_result_set_update_description (result, "<p>Hi there!</p>");
	fwupd_result_set_update_filename (result, "firmware.bin");
	fwupd_result_set_update_id (result, "org.dave.ColorHug.firmware");
	fwupd_result_set_update_size (result, 1024);
	fwupd_result_set_update_uri (result, "http://foo.com");
	fwupd_result_set_update_version (result, "1.2.3");
	fwupd_result_add_device_flag (result, FWUPD_DEVICE_FLAG_REQUIRE_AC);
	fwupd_result_set_update_trust_flags (result, FWUPD_TRUST_FLAG_PAYLOAD);
	str = fwupd_result_to_string (result);
	g_print ("\n%s", str);

	/* check GUIDs */
	g_assert (fwupd_result_has_guid (result, "2082b5e0-7a64-478a-b1b2-e3404fab6dad"));
	g_assert (fwupd_result_has_guid (result, "00000000-0000-0000-0000-000000000000"));
	g_assert (!fwupd_result_has_guid (result, "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"));

	ret = as_test_compare_lines (str,
		"ColorHug2\n"
		"  Guid:                 2082b5e0-7a64-478a-b1b2-e3404fab6dad\n"
		"  Guid:                 00000000-0000-0000-0000-000000000000\n"
		"  DeviceID:             USB:foo\n"
		"  Flags:                allow-offline|require-ac\n"
		"  FirmwareHash:         beefdead\n"
		"  DeviceChecksumKind:   sha256\n"
		"  Created:              1970-01-01\n"
		"  Modified:             1970-01-02\n"
		"  AppstreamId:          org.dave.ColorHug.firmware\n"
		"  UpdateDescription:    <p>Hi there!</p>\n"
		"  UpdateVersion:        1.2.3\n"
		"  FilenameCab:          firmware.bin\n"
		"  UpdateHash:           deadbeef\n"
		"  UpdateChecksumKind:   sha1\n"
		"  Size:                 1.0 kB\n"
		"  UpdateUri:            http://foo.com\n"
		"  Trusted:              payload\n", &error);
	g_assert_no_error (error);
	g_assert (ret);
}

static void
fwupd_client_devices_func (void)
{
	FwupdResult *res;
	g_autoptr(FwupdClient) client = NULL;
	g_autoptr(GPtrArray) array = NULL;
	g_autoptr(GError) error = NULL;

	client = fwupd_client_new ();
	array = fwupd_client_get_devices (client, NULL, &error);
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
	g_assert_cmpstr (fwupd_result_get_guid_default (res), !=, NULL);
	g_assert_cmpstr (fwupd_result_get_device_id (res), !=, NULL);
}

static void
fwupd_client_updates_func (void)
{
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
	g_assert_cmpstr (fwupd_result_get_guid_default (res), !=, NULL);
	g_assert_cmpstr (fwupd_result_get_device_id (res), !=, NULL);
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

	/* tests go here */
	g_test_add_func ("/fwupd/enums", fwupd_enums_func);
	g_test_add_func ("/fwupd/result", fwupd_result_func);
	if (fwupd_has_system_bus ()) {
		g_test_add_func ("/fwupd/client{devices}", fwupd_client_devices_func);
		g_test_add_func ("/fwupd/client{updates}", fwupd_client_updates_func);
	}
	return g_test_run ();
}
