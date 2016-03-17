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

#include "fwupd-result.h"

/**
 * as_test_compare_lines:
 **/
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
fwupd_result_func (void)
{
	gboolean ret;
	g_autofree gchar *str = NULL;
	g_autoptr(FwupdResult) result = NULL;
	g_autoptr(GError) error = NULL;

	/* create dummy object */
	result = fwupd_result_new ();
	fwupd_result_set_device_checksum (result, "beefdead");
	fwupd_result_set_device_created (result, 1);
	fwupd_result_set_device_flags (result, FU_DEVICE_FLAG_ALLOW_OFFLINE);
	fwupd_result_set_device_id (result, "USB:foo");
	fwupd_result_set_device_modified (result, 60 * 60 * 24);
	fwupd_result_set_device_name (result, "ColorHug2");
	fwupd_result_set_guid (result, "2082b5e0-7a64-478a-b1b2-e3404fab6dad");
	fwupd_result_set_update_checksum (result, "deadbeef");
	fwupd_result_set_update_description (result, "<p>Hi there!</p>");
	fwupd_result_set_update_filename (result, "firmware.bin");
	fwupd_result_set_update_id (result, "org.dave.ColorHug.firmware");
	fwupd_result_set_update_size (result, 1024);
	fwupd_result_set_update_uri (result, "http://foo.com");
	fwupd_result_add_device_flag (result, FU_DEVICE_FLAG_REQUIRE_AC);
	fwupd_result_set_update_trust_flags (result, FWUPD_TRUST_FLAG_PAYLOAD);
	str = fwupd_result_to_string (result);
	g_print ("\n%s", str);

	ret = as_test_compare_lines (str,
		"USB:foo\n"
		"  Guid:                 2082b5e0-7a64-478a-b1b2-e3404fab6dad\n"
		"  DisplayName:          ColorHug2\n"
		"  Flags:                allow-offline|require-ac\n"
		"  FirmwareHash:         beefdead\n"
		"  Created:              1970-01-01\n"
		"  Modified:             1970-01-02\n"
		"  AppstreamId:          org.dave.ColorHug.firmware\n"
		"  UpdateDescription:    <p>Hi there!</p>\n"
		"  FilenameCab:          firmware.bin\n"
		"  UpdateHash:           deadbeef\n"
		"  Size:                 1.0 kB\n"
		"  UpdateUri:            http://foo.com\n"
		"  Trusted:              payload\n", &error);
	g_assert_no_error (error);
	g_assert (ret);
}

int
main (int argc, char **argv)
{
	g_test_init (&argc, &argv, NULL);

	/* only critical and error are fatal */
	g_log_set_fatal_mask (NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);

	/* tests go here */
	g_test_add_func ("/fwupd/result", fwupd_result_func);
	return g_test_run ();
}

