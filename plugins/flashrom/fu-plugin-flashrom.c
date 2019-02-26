/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
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

#include <string.h>

#include "fu-plugin-vfuncs.h"

struct FuPluginData {
	gchar			*flashrom_fn;
};

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_plugin_alloc_data (plugin, sizeof (FuPluginData));
	fu_plugin_add_rule (plugin, FU_PLUGIN_RULE_SUPPORTS_PROTOCOL, "org.flashrom");
}

void
fu_plugin_destroy (FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	g_free (data->flashrom_fn);
}

gboolean
fu_plugin_startup (FuPlugin *plugin, GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	GPtrArray *hwids;
	g_autoptr(GError) error_local = NULL;

	/* we need flashrom from the host system */
	data->flashrom_fn = fu_common_find_program_in_path ("flashrom", &error_local);

	/* search for devices */
	hwids = fu_plugin_get_hwids (plugin);
	for (guint i = 0; i < hwids->len; i++) {
		const gchar *guid = g_ptr_array_index (hwids, i);
		const gchar *quirk_str;
		g_autofree gchar *quirk_key_prefixed = NULL;
		quirk_key_prefixed = g_strdup_printf ("HwId=%s", guid);
		quirk_str = fu_plugin_lookup_quirk_by_id (plugin,
							  quirk_key_prefixed,
							  "DeviceId");
		if (quirk_str != NULL) {
			g_autofree gchar *device_id = g_strdup_printf ("flashrom-%s", quirk_str);
			g_autoptr(FuDevice) dev = fu_device_new ();
			fu_device_set_id (dev, device_id);
			fu_device_set_quirks (dev, fu_plugin_get_quirks (plugin));
			fu_device_add_flag (dev, FWUPD_DEVICE_FLAG_INTERNAL);
			if (data->flashrom_fn != NULL) {
				fu_device_add_flag (dev, FWUPD_DEVICE_FLAG_UPDATABLE);
			} else {
				fu_device_set_update_error (dev, error_local->message);
			}
			fu_device_add_guid (dev, guid);
			fu_device_set_name (dev, fu_plugin_get_dmi_value (plugin, FU_HWIDS_KEY_PRODUCT_NAME));
			fu_device_set_vendor (dev, fu_plugin_get_dmi_value (plugin, FU_HWIDS_KEY_MANUFACTURER));
			fu_device_set_version (dev, fu_plugin_get_dmi_value (plugin, FU_HWIDS_KEY_BIOS_VERSION));
			fu_plugin_device_add (plugin, dev);
			fu_plugin_cache_add (plugin, device_id, dev);
			break;
		}
	}
	return TRUE;
}

static guint
fu_plugin_flashrom_parse_percentage (const gchar *lines_verbose)
{
	const guint64 addr_highest = 0x800000;
	guint64 addr_best = 0x0;
	g_auto(GStrv) chunks = NULL;

	/* parse 0x000000-0x000fff:S, 0x001000-0x001fff:S */
	chunks = g_strsplit_set (lines_verbose, "x-:S, \n\r", -1);
	for (guint i = 0; chunks[i] != NULL; i++) {
		guint64 addr_tmp;
		if (strlen (chunks[i]) != 6)
			continue;
		addr_tmp = g_ascii_strtoull (chunks[i], NULL, 16);
		if (addr_tmp > addr_best)
			addr_best = addr_tmp;
	}
	return (addr_best * 100) / addr_highest;
}

static void
fu_plugin_flashrom_read_cb (const gchar *line, gpointer user_data)
{
	FuDevice *device = FU_DEVICE (user_data);
	if (g_strcmp0 (line, "Reading flash...") == 0)
		fu_device_set_status (device, FWUPD_STATUS_DEVICE_VERIFY);
	fu_device_set_progress (device, fu_plugin_flashrom_parse_percentage (line));
}

static void
fu_plugin_flashrom_write_cb (const gchar *line, gpointer user_data)
{
	FuDevice *device = FU_DEVICE (user_data);
	if (g_strcmp0 (line, "Writing flash...") == 0)
		fu_device_set_status (device, FWUPD_STATUS_DEVICE_WRITE);
	fu_device_set_progress (device, fu_plugin_flashrom_parse_percentage (line));
}

gboolean
fu_plugin_update_prepare (FuPlugin *plugin,
			  FwupdInstallFlags flags,
			  FuDevice *device,
			  GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	g_autofree gchar *firmware_orig = NULL;
	g_autofree gchar *basename = NULL;

	/* not us */
	if (fu_plugin_cache_lookup (plugin, fu_device_get_id (device)) == NULL)
		return TRUE;

	/* if the original firmware doesn't exist, grab it now */
	basename = g_strdup_printf ("flashrom-%s.bin", fu_device_get_id (device));
	firmware_orig = g_build_filename (LOCALSTATEDIR, "lib", "fwupd",
					  "builder", basename, NULL);
	if (!fu_common_mkdir_parent (firmware_orig, error))
		return FALSE;
	if (!g_file_test (firmware_orig, G_FILE_TEST_EXISTS)) {
		const gchar *argv[] = {
			data->flashrom_fn,
			"--programmer", "internal:laptop=force_I_want_a_brick",
			"--read", firmware_orig,
			"--verbose", NULL };
		if (!fu_common_spawn_sync ((const gchar * const *) argv,
					   fu_plugin_flashrom_read_cb, device,
					   0, NULL, error)) {
			g_prefix_error (error, "failed to get original firmware: ");
			return FALSE;
		}
	}

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
	g_autofree gchar *firmware_fn = NULL;
	g_autofree gchar *tmpdir = NULL;
	const gchar *argv[] = {
		data->flashrom_fn,
		"--programmer", "internal:laptop=force_I_want_a_brick",
		"--write", "xxx",
		"--verbose", NULL };

	/* write blob to temp location */
	tmpdir = g_dir_make_tmp ("fwupd-XXXXXX", error);
	if (tmpdir == NULL)
		return FALSE;
	firmware_fn = g_build_filename (tmpdir, "flashrom-firmware.bin", NULL);
	if (!fu_common_set_contents_bytes (firmware_fn, blob_fw, error))
		return FALSE;

	/* use flashrom to write image */
	argv[4] = firmware_fn;
	if (!fu_common_spawn_sync ((const gchar * const *) argv,
				   fu_plugin_flashrom_write_cb, device,
				   0, NULL, error)) {
		g_prefix_error (error, "failed to write firmware: ");
		return FALSE;
	}

	/* delete temp location */
	if (!fu_common_rmtree (tmpdir, error))
		return FALSE;

	/* success */
	return TRUE;
}
