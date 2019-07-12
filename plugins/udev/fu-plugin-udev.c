/*
 * Copyright (C) 2015-2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-plugin.h"
#include "fu-rom.h"
#include "fu-plugin-vfuncs.h"

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
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
			 fu_device_get_id (device),
			 fu_device_get_version (device),
			 fu_rom_get_version (rom));
		fu_device_set_version (device, fu_rom_get_version (rom),
				       FWUPD_VERSION_FORMAT_UNKNOWN);
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
fu_plugin_udev_device_added (FuPlugin *plugin, FuUdevDevice *device, GError **error)
{
	GUdevDevice *udev_device = fu_udev_device_get_dev (FU_UDEV_DEVICE (device));
	const gchar *guid = NULL;
	g_autofree gchar *rom_fn = NULL;

	/* interesting device? */
	if (g_strcmp0 (fu_udev_device_get_subsystem (device), "pci") != 0)
		return TRUE;
	guid = g_udev_device_get_property (udev_device, "FWUPD_GUID");
	if (guid == NULL)
		return TRUE;

	/* set the physical ID */
	if (!fu_udev_device_set_physical_id (device, "pci", error))
		return FALSE;

	/* did we get enough data */
	fu_device_add_flag (FU_DEVICE (device), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_icon (FU_DEVICE (device), "audio-card");

	/* get the FW version from the rom when unlocked */
	rom_fn = g_build_filename (fu_udev_device_get_sysfs_path (device), "rom", NULL);
	if (g_file_test (rom_fn, G_FILE_TEST_EXISTS))
		fu_device_set_metadata (FU_DEVICE (device), "RomFilename", rom_fn);

	/* we never open the device, so convert the instance IDs */
	if (!fu_device_setup (FU_DEVICE (device), error))
		return FALSE;

	/* insert to hash */
	fu_plugin_device_add (plugin, FU_DEVICE (device));
	return TRUE;
}
