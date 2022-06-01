/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

static void
fu_plugin_uefi_recovery_init(FuPlugin *plugin)
{
	/* make sure that UEFI plugin is ready to receive devices */
	fu_plugin_add_rule(plugin, FU_PLUGIN_RULE_RUN_AFTER, "uefi_capsule");
	fu_plugin_add_flag(plugin, FWUPD_PLUGIN_FLAG_REQUIRE_HWID);
}

static gboolean
fu_plugin_uefi_recovery_startup(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	/* are the EFI dirs set up so we can update each device */
	if (!fu_efivar_supported(error))
		return FALSE;
	return TRUE;
}

static gboolean
fu_plugin_uefi_recovery_coldplug(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	GPtrArray *hwids = fu_context_get_hwid_guids(ctx);
	const gchar *dmi_vendor;
	g_autoptr(FuDevice) device = fu_device_new(fu_plugin_get_context(plugin));
	fu_device_set_id(device, "uefi-recovery");
	fu_device_set_name(device, "System Firmware ESRT Recovery");
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device, "0.0.0");
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_REQUIRE_AC);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_NEEDS_REBOOT);
	fu_device_set_metadata(device, FU_DEVICE_METADATA_UEFI_DEVICE_KIND, "system-firmware");
	fu_device_add_icon(device, "computer");
	for (guint i = 0; i < hwids->len; i++) {
		const gchar *hwid = g_ptr_array_index(hwids, i);
		fu_device_add_guid(device, hwid);
	}

	/* set vendor ID as the BIOS vendor */
	dmi_vendor = fu_context_get_hwid_value(ctx, FU_HWIDS_KEY_BIOS_VENDOR);
	if (dmi_vendor != NULL) {
		g_autofree gchar *vendor_id = g_strdup_printf("DMI:%s", dmi_vendor);
		fu_device_add_vendor_id(device, vendor_id);
	}

	fu_plugin_device_register(plugin, device);
	return TRUE;
}

void
fu_plugin_init_vfuncs(FuPluginVfuncs *vfuncs)
{
	vfuncs->build_hash = FU_BUILD_HASH;
	vfuncs->init = fu_plugin_uefi_recovery_init;
	vfuncs->coldplug = fu_plugin_uefi_recovery_coldplug;
	vfuncs->startup = fu_plugin_uefi_recovery_startup;
}
