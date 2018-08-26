/*
 * Copyright (C) 2015-2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <appstream-glib.h>

#include "fu-plugin.h"
#include "fu-rom.h"
#include "fu-plugin-vfuncs.h"

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_add_udev_subsystem (plugin, "pci");
}

gboolean
fu_plugin_verify (FuPlugin *plugin,
		  FuDevice *device,
		  FuPluginVerifyFlags flags,
		  GError **error)
{
	GPtrArray *checksums;
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

	/* update version */
	if (g_strcmp0 (fu_device_get_version (device),
		       fu_rom_get_version (rom)) != 0) {
		g_debug ("changing version of %s from %s to %s",
			 fu_device_get_platform_id (device),
			 fu_device_get_version (device),
			 fu_rom_get_version (rom));
		fu_device_set_version (device, fu_rom_get_version (rom));
	}

	/* Also add the GUID from the firmware as the firmware may be more
	 * generic, which also allows us to match the GUID when doing 'verify'
	 * on a device with a different PID to the firmware */
	fu_device_add_guid (device, fu_rom_get_guid (rom));

	/* update checksums */
	checksums = fu_rom_get_checksums (rom);
	for (guint i = 0; i < checksums->len; i++) {
		const gchar *checksum = g_ptr_array_index (checksums, i);
		fu_device_add_checksum (device, checksum);
	}
	return TRUE;
}

gboolean
fu_plugin_udev_device_added (FuPlugin *plugin, GUdevDevice *udev_device, GError **error)
{
	FuDevice *dev_tmp;
	const gchar *guid = NULL;
	const gchar *id = NULL;
	g_autofree gchar *rom_fn = NULL;
	g_autoptr(AsProfile) profile = as_profile_new ();
	g_autoptr(AsProfileTask) ptask = NULL;
	g_autoptr(FuDevice) dev = NULL;

	/* interesting device? */
	if (g_strcmp0 (g_udev_device_get_subsystem (udev_device), "pci") != 0)
		return TRUE;
	guid = g_udev_device_get_property (udev_device, "FWUPD_GUID");
	if (guid == NULL)
		return TRUE;

	/* get data */
	ptask = as_profile_start (profile, "FuPluginUdev:client-add{%s}", guid);
	g_assert (ptask != NULL);
	g_debug ("adding udev device: %s", g_udev_device_get_sysfs_path (udev_device));

	/* is already in database */
	id = g_udev_device_get_sysfs_path (udev_device);
	dev_tmp = fu_plugin_cache_lookup (plugin, id);
	if (dev_tmp != NULL) {
		g_debug ("ignoring duplicate %s", id);
		return TRUE;
	}

	/* did we get enough data */
	dev = fu_udev_device_new (udev_device);
	fu_device_set_quirks (dev, fu_plugin_get_quirks (plugin));
	fu_device_add_flag (dev, FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_guid (dev, guid);
	fu_device_add_icon (dev, "audio-card");

	/* get the FW version from the rom when unlocked */
	rom_fn = g_build_filename (g_udev_device_get_sysfs_path (udev_device), "rom", NULL);
	if (g_file_test (rom_fn, G_FILE_TEST_EXISTS))
		fu_device_set_metadata (dev, "RomFilename", rom_fn);

	/* insert to hash */
	fu_plugin_cache_add (plugin, id, dev);
	fu_plugin_device_add_delay (plugin, dev);
	return TRUE;
}
