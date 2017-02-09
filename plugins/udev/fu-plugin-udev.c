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
#include <gudev/gudev.h>

#include "fu-plugin.h"
#include "fu-rom.h"
#include "fu-plugin-vfuncs.h"

struct FuPluginData {
	GUdevClient		*gudev_client;
};

//FIXME: this needs to move to the plugin core
static gchar *
fu_plugin_udev_get_id (GUdevDevice *device)
{
	gchar *id;
	id = g_strdup_printf ("ro-%s", g_udev_device_get_sysfs_path (device));
	g_strdelimit (id, "/:.-", '_');
	return id;
}

gboolean
fu_plugin_unlock (FuPlugin *plugin, FuDevice *device, GError **error)
{
	const gchar *rom_fn;
	g_autoptr(FuRom) rom = NULL;
	g_autoptr(GFile) file = NULL;

	/* get the FW version from the rom */
	g_debug ("unlocking UDev device %s", fu_device_get_id (device));
	rom_fn = fu_device_get_metadata (device, "RomFilename");
	if (rom_fn == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Unable to read firmware from device");
		return FALSE;
	}
	file = g_file_new_for_path (rom_fn);
	rom = fu_rom_new ();
	if (!fu_rom_load_file (rom, file, FU_ROM_LOAD_FLAG_BLANK_PPID, NULL, error))
		return FALSE;

	/* update version */
	if (g_strcmp0 (fu_device_get_version (device),
		       fu_rom_get_version (rom)) != 0) {
		g_debug ("changing version of %s from %s to %s",
			 fu_device_get_id (device),
			 fu_device_get_version (device),
			 fu_rom_get_version (rom));
		fu_device_set_version (device, fu_rom_get_version (rom));
	}

	/* Also add the GUID from the firmware as the firmware may be more
	 * generic, which also allows us to match the GUID when doing 'verify'
	 * on a device with a different PID to the firmware */
	fu_device_add_guid (device, fu_rom_get_guid (rom));

	return TRUE;
}

gboolean
fu_plugin_verify (FuPlugin *plugin,
		  FuDevice *device,
		  FuPluginVerifyFlags flags,
		  GError **error)
{
	const gchar *rom_fn;
	g_autoptr(GFile) file = NULL;
	g_autoptr(FuRom) rom = NULL;

	/* open the file */
	rom_fn = fu_device_get_metadata (device, "RomFilename");
	if (rom_fn == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Unable to read firmware from device");
		return FALSE;
	}
	file = g_file_new_for_path (rom_fn);
	rom = fu_rom_new ();
	if (!fu_rom_load_file (rom, file, FU_ROM_LOAD_FLAG_BLANK_PPID, NULL, error))
		return FALSE;
	fu_device_set_checksum (device, fu_rom_get_checksum (rom));
	return TRUE;
}

static void
fu_plugin_udev_add (FuPlugin *plugin, GUdevDevice *device)
{
	FuDevice *dev_tmp;
	const gchar *display_name;
	const gchar *guid;
	const gchar *product;
	const gchar *vendor;
	g_autofree gchar *id = NULL;
	g_autofree gchar *rom_fn = NULL;
	g_autofree gchar *version = NULL;
	g_auto(GStrv) split = NULL;
	g_autoptr(AsProfile) profile = as_profile_new ();
	g_autoptr(AsProfileTask) ptask = NULL;
	g_autoptr(FuDevice) dev = NULL;

	/* interesting device? */
	guid = g_udev_device_get_property (device, "FWUPD_GUID");
	if (guid == NULL)
		return;

	/* get data */
	ptask = as_profile_start (profile, "FuPluginUdev:client-add{%s}", guid);
	g_assert (ptask != NULL);
	g_debug ("adding udev device: %s", g_udev_device_get_sysfs_path (device));

	/* is already in database */
	id = fu_plugin_udev_get_id (device);
	dev_tmp = fu_plugin_cache_lookup (plugin, id);
	if (dev_tmp != NULL) {
		g_debug ("ignoring duplicate %s", id);
		return;
	}

	/* get the FW version from the BCD device revision */
	product = g_udev_device_get_property (device, "PRODUCT");
	if (product != NULL) {
		split = g_strsplit (product, "/", -1);
		if (g_strv_length (split) != 3) {
			g_warning ("env{PRODUCT} is invalid: %s", product);
			return;
		}
		version = g_strdup (split[2]);
	}

	/* did we get enough data */
	dev = fu_device_new ();
	fu_device_add_flag (dev, FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_set_id (dev, id);
	fu_device_add_guid (dev, guid);
	display_name = g_udev_device_get_property (device, "FWUPD_MODEL");
	if (display_name == NULL)
		display_name = g_udev_device_get_property (device, "ID_MODEL_FROM_DATABASE");
	if (display_name != NULL)
		fu_device_set_name (dev, display_name);
	vendor = g_udev_device_get_property (device, "FWUPD_VENDOR");
	if (vendor == NULL)
		vendor = g_udev_device_get_property (device, "ID_VENDOR_FROM_DATABASE");
	if (vendor != NULL)
		fu_device_set_vendor (dev, vendor);
	if (version != NULL)
		fu_device_set_version (dev, version);

	/* get the FW version from the rom when unlocked */
	rom_fn = g_build_filename (g_udev_device_get_sysfs_path (device), "rom", NULL);
	if (g_file_test (rom_fn, G_FILE_TEST_EXISTS)) {
		fu_device_set_metadata (dev, "RomFilename", rom_fn);
		fu_device_add_flag (dev, FWUPD_DEVICE_FLAG_LOCKED);
	}

	/* insert to hash */
	fu_plugin_cache_add (plugin, id, dev);
	fu_plugin_device_add_delay (plugin, dev);
}

static void
fu_plugin_udev_remove (FuPlugin *plugin, GUdevDevice *device)
{
	FuDevice *dev;
	g_autofree gchar *id = NULL;

	/* interesting device? */
	if (g_udev_device_get_property (device, "FWUPD_GUID") == NULL)
		return;

	/* already in database */
	id = fu_plugin_udev_get_id (device);
	dev = fu_plugin_cache_lookup (plugin, id);
	if (dev == NULL)
		return;
	fu_plugin_device_remove (plugin, dev);
}

static void
fu_plugin_udev_uevent_cb (GUdevClient *gudev_client,
			  const gchar *action,
			  GUdevDevice *udev_device,
			  FuPlugin *plugin)
{
	if (g_strcmp0 (action, "remove") == 0) {
		fu_plugin_udev_remove (plugin, udev_device);
		return;
	}
	if (g_strcmp0 (action, "add") == 0) {
		fu_plugin_udev_add (plugin, udev_device);
		return;
	}
}

void
fu_plugin_init (FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_alloc_data (plugin, sizeof (FuPluginData));
	const gchar *subsystems[] = { NULL };

	data->gudev_client = g_udev_client_new (subsystems);
	g_signal_connect (data->gudev_client, "uevent",
			  G_CALLBACK (fu_plugin_udev_uevent_cb), plugin);
}

void
fu_plugin_destroy (FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	g_object_unref (data->gudev_client);
}

gboolean
fu_plugin_coldplug (FuPlugin *plugin, GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	GList *devices;
	GUdevDevice *udev_device;
	const gchar *devclass[] = { "usb", "pci", NULL };
	g_autoptr(AsProfile) profile = as_profile_new ();

	/* get all devices of class */
	for (guint i = 0; devclass[i] != NULL; i++) {
		g_autoptr(AsProfileTask) ptask = NULL;
		ptask = as_profile_start (profile, "FuPluginUdev:coldplug{%s}", devclass[i]);
		g_assert (ptask != NULL);
		devices = g_udev_client_query_by_subsystem (data->gudev_client,
							    devclass[i]);
		for (GList *l = devices; l != NULL; l = l->next) {
			udev_device = l->data;
			fu_plugin_udev_add (plugin, udev_device);
		}
		g_list_foreach (devices, (GFunc) g_object_unref, NULL);
		g_list_free (devices);
	}
	return TRUE;
}
