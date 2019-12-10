/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-device-metadata.h"
#include "fu-plugin-vfuncs.h"
#include "fu-hash.h"

void
fu_plugin_init (FuPlugin *plugin)
{
	/* make sure that UEFI plugin is ready to receive devices */
	fu_plugin_add_rule (plugin, FU_PLUGIN_RULE_RUN_AFTER, "uefi");
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
}

gboolean
fu_plugin_startup (FuPlugin *plugin, GError **error)
{
	if (!fu_plugin_has_custom_flag (plugin, "requires-uefi-recovery")) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "not required");
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_plugin_coldplug (FuPlugin *plugin, GError **error)
{
	GPtrArray *hwids = fu_plugin_get_hwids (plugin);
	const gchar *dmi_vendor;
	g_autoptr(FuDevice) device = fu_device_new ();
	fu_device_set_id (device, "uefi-recovery");
	fu_device_set_name (device, "System Firmware ESRT Recovery");
	fu_device_set_version (device, "0.0.0", FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_REQUIRE_AC);
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_NEEDS_REBOOT);
	fu_device_set_metadata (device, FU_DEVICE_METADATA_UEFI_DEVICE_KIND, "system-firmware");
	fu_device_add_icon (device, "computer");
	for (guint i = 0; i < hwids->len; i++) {
		const gchar *hwid = g_ptr_array_index (hwids, i);
		fu_device_add_guid (device, hwid);
	}

	/* set vendor ID as the BIOS vendor */
	dmi_vendor = fu_plugin_get_dmi_value (plugin, FU_HWIDS_KEY_BIOS_VENDOR);
	if (dmi_vendor != NULL) {
		g_autofree gchar *vendor_id = g_strdup_printf ("DMI:%s", dmi_vendor);
		fu_device_set_vendor_id (device, vendor_id);
	}

	fu_plugin_device_register (plugin, device);
	return TRUE;
}
