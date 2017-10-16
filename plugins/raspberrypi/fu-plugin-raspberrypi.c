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

#include <appstream-glib.h>
#include <string.h>

#include "fu-plugin.h"
#include "fu-plugin-vfuncs.h"
#include "fu-plugin-raspberrypi.h"

#define FU_PLUGIN_RPI_FIRMWARE_FILENAME		"start.elf"

struct FuPluginData {
	gchar			*fw_dir;
};

static gchar *
fu_plugin_raspberrypi_strstr (const guint8 *haystack,
			gsize haystack_len,
			const gchar *needle,
			gsize *offset)
{
	gsize needle_len;

	if (needle == NULL || needle[0] == '\0')
		return NULL;
	if (haystack == NULL || haystack_len == 0)
		return NULL;
	needle_len = strlen (needle);
	if (needle_len > haystack_len)
		return NULL;
	for (gsize i = 0; i < haystack_len - needle_len; i++) {
		if (memcmp (haystack + i, needle, needle_len) == 0) {
			if (offset != NULL)
				*offset = i + needle_len;
			return g_strdup ((const gchar *) &haystack[i + needle_len]);
		}
	}
	return NULL;
}

static gboolean
fu_plugin_raspberrypi_parse_firmware (FuDevice *device, const gchar *fn, GError **error)
{
	GDate *date;
	gsize len = 0;
	gsize offset;
	g_autofree gchar *fwver = NULL;
	g_autofree gchar *platform = NULL;
	g_autofree gchar *vc_date = NULL;
	g_autofree gchar *vc_time = NULL;
	g_autofree guint8 *data = NULL;

	/* read file -- things we can find are:
	 *
	 * VC_BUILD_ID_USER: dc4
	 * VC_BUILD_ID_TIME: 14:58:37
	 * VC_BUILD_ID_BRANCH: master
	 * VC_BUILD_ID_TIME: Aug  3 2015
	 * VC_BUILD_ID_HOSTNAME: dc4-XPS13-9333
	 * VC_BUILD_ID_PLATFORM: raspberrypi_linux
	 * VC_BUILD_ID_VERSION: 4b51d81eb0068a875b336f4cc2c468cbdd06d0c5 (clean)
	 */
	if (!g_file_get_contents (fn, (gchar **) &data, &len, error))
		return FALSE;

	/* check the platform matches */
	platform = fu_plugin_raspberrypi_strstr (data, len,
					   "VC_BUILD_ID_PLATFORM: ",
					   NULL);
	if (g_strcmp0 (platform, "raspberrypi_linux") != 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "not a RasberryPi, platform is %s",
			     platform);
		return FALSE;
	}

	/* find the VC_BUILD info which paradoxically is split into two
	 * string segments */
	vc_time = fu_plugin_raspberrypi_strstr (data, len,
					  "VC_BUILD_ID_TIME: ", &offset);
	if (vc_time == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "Failed to get 1st VC_BUILD_ID_TIME");
		return FALSE;
	}
	vc_date = fu_plugin_raspberrypi_strstr (data + offset, len - offset,
					  "VC_BUILD_ID_TIME: ", NULL);
	if (vc_date == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "Failed to get 2nd VC_BUILD_ID_TIME");
		return FALSE;
	}

	/* parse the date */
	date = g_date_new ();
	g_date_set_parse (date, vc_date);
	if (!g_date_valid (date)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "Failed to parse date '%s'",
			     vc_date);
		return FALSE;
	}

	/* create a version number from the date and time */
	fwver = g_strdup_printf ("%04i%02u%02i",
				 g_date_get_year (date),
				 g_date_get_month (date),
				 g_date_get_day (date));
	fu_device_set_version (device, fwver);

	g_date_free (date);
	return TRUE;
}

gboolean
fu_plugin_update (FuPlugin *plugin,
		  FuDevice *device,
		  GBytes *blob_fw,
		  FwupdInstallFlags flags,
		  GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	g_autofree gchar *fwfn = NULL;

	/* decompress anything matching either glob */
	fu_plugin_set_status (plugin, FWUPD_STATUS_DEVICE_WRITE);
	if (!fu_common_extract_archive (blob_fw, data->fw_dir, error))
		return FALSE;

	/* get the new VC build info */
	fu_plugin_set_status (plugin, FWUPD_STATUS_DEVICE_VERIFY);
	fwfn = g_build_filename (data->fw_dir,
				 FU_PLUGIN_RPI_FIRMWARE_FILENAME,
				 NULL);
	return fu_plugin_raspberrypi_parse_firmware (device, fwfn, error);
}

void
fu_plugin_raspberrypi_set_fw_dir (FuPlugin *plugin, const gchar *fw_dir)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	g_free (data->fw_dir);
	data->fw_dir = g_strdup (fw_dir);
	g_mkdir_with_parents (fw_dir, 0700);
}

void
fu_plugin_init (FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_alloc_data (plugin, sizeof (FuPluginData));
	const gchar *tmp;

	/* allow this to be overidden for testing */
	data->fw_dir = g_strdup ("/boot");
	tmp = g_getenv ("FWUPD_RPI_FW_DIR");
	if (tmp != NULL)
		fu_plugin_raspberrypi_set_fw_dir (plugin, tmp);
}

void
fu_plugin_destroy (FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	g_free (data->fw_dir);
}

gboolean
fu_plugin_coldplug (FuPlugin *plugin, GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	g_autofree gchar *fwfn = NULL;
	g_autoptr(FuDevice) device = NULL;

	/* anything interesting */
	fwfn = g_build_filename (data->fw_dir,
				 FU_PLUGIN_RPI_FIRMWARE_FILENAME,
				 NULL);
	if (!g_file_test (fwfn, G_FILE_TEST_EXISTS)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "Raspberry PI firmware updating not supported, no %s",
			     fwfn);
		return FALSE;
	}

	/* create fake device */
	device = fu_device_new ();
	fu_device_set_id (device, "raspberry-pi");
	fu_device_add_guid (device, "raspberrypi");
	fu_device_set_name (device, "Raspberry Pi");
	fu_device_set_vendor (device, "Raspberry Pi Foundation");
	fu_device_set_summary (device, "A tiny and affordable computer");
	fu_device_add_icon (device, "computer");
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_REQUIRE_AC);

	/* get the VC build info */
	if (!fu_plugin_raspberrypi_parse_firmware (device, fwfn, error))
		return FALSE;

	fu_plugin_device_add (plugin, device);
	return TRUE;
}
