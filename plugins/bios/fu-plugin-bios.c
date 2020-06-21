/*
 * Copyright (C) 2020 Mario Limonciello <mario.limonciello@dell.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-efivar.h"
#include "fu-plugin-vfuncs.h"
#include "fu-hash.h"

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
}


gboolean
fu_plugin_startup (FuPlugin *plugin, GError **error)
{
	const gchar *vendor;

	vendor = fu_plugin_get_dmi_value (plugin, FU_HWIDS_KEY_BIOS_VENDOR);
	if (g_strcmp0 (vendor, "coreboot") == 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_FOUND,
			     "system uses coreboot");
		return FALSE;
	}

	return TRUE;
}


static gboolean
fu_plugin_bios_create_dummy (FuPlugin *plugin, const gchar *reason, GError **error)
{
	const gchar *key;
	g_autoptr(FuDevice) dev = fu_device_new ();

	fu_device_set_version_format (dev, FWUPD_VERSION_FORMAT_PLAIN);
	key = fu_plugin_get_dmi_value (plugin, FU_HWIDS_KEY_MANUFACTURER);
	if (key != NULL)
		fu_device_set_vendor (dev, key);
	key = fu_plugin_get_dmi_value (plugin, FU_HWIDS_KEY_BIOS_VENDOR);
	if (key != NULL) {
		g_autofree gchar *vendor_id = g_strdup_printf ("DMI:%s", key);
		fu_device_set_vendor_id (FU_DEVICE (dev), vendor_id);
	}
	key = "System Firmware";
	fu_device_set_name (dev, key);
	key = fu_plugin_get_dmi_value (plugin, FU_HWIDS_KEY_BIOS_VERSION);
	if (key != NULL)
		fu_device_set_version (dev, key);
	fu_device_set_update_error (dev, reason);

	fu_device_add_flag (dev, FWUPD_DEVICE_FLAG_INTERNAL);

	fu_device_add_icon (dev, "computer");
	fu_device_set_id (dev, "BIOS-dummy");
	fu_device_add_instance_id (dev, "main-system-firmware");
	if (!fu_device_setup (dev, error))
		return FALSE;
	fu_plugin_device_add (plugin, dev);

	return TRUE;
}

gboolean
fu_plugin_coldplug (FuPlugin *plugin, GError **error)
{
	g_autofree gchar *sysfsfwdir = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autofree gchar *esrt_path = NULL;

	/* are the EFI dirs set up so we can update each device */
	if (!fu_efivar_supported (&error_local)) {
		const gchar *reason = "Firmware can not be updated in legacy BIOS mode, switch to UEFI mode";
		g_warning ("%s", error_local->message);
		return fu_plugin_bios_create_dummy (plugin, reason, error);
	}

	/* get the directory of ESRT entries */
	sysfsfwdir = fu_common_get_path (FU_PATH_KIND_SYSFSDIR_FW);
	esrt_path = g_build_filename (sysfsfwdir, "efi", "esrt", NULL);
	if (!g_file_test (esrt_path, G_FILE_TEST_IS_DIR)) {
		const gchar *reason = "UEFI Capsule updates not available or enabled";
		return fu_plugin_bios_create_dummy (plugin, reason, error);
	}

	/* we appear to have UEFI capsule updates */
	fu_plugin_set_enabled (plugin, FALSE);

	return TRUE;
}
