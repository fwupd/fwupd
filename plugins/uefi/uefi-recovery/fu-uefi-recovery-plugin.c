/*
 * Copyright 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-uefi-recovery-plugin.h"

struct _FuUefiRecoveryPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuUefiRecoveryPlugin, fu_uefi_recovery_plugin, FU_TYPE_PLUGIN)

static gboolean
fu_uefi_recovery_plugin_startup(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	FuEfivars *efivars = fu_context_get_efivars(ctx);
	/* are the EFI dirs set up so we can update each device */
	return fu_efivars_supported(efivars, error);
}

static gboolean
fu_uefi_recovery_plugin_coldplug(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	GPtrArray *hwids = fu_context_get_hwid_guids(ctx);
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
	fu_device_add_icon(device, FU_DEVICE_ICON_COMPUTER);
	for (guint i = 0; i < hwids->len; i++) {
		const gchar *hwid = g_ptr_array_index(hwids, i);
		fu_device_add_instance_id(device, hwid);
	}

	/* set vendor ID as the BIOS vendor */
	fu_device_build_vendor_id(device,
				  "DMI",
				  fu_context_get_hwid_value(ctx, FU_HWIDS_KEY_BIOS_VENDOR));

	fu_plugin_device_register(plugin, device);
	return TRUE;
}

static void
fu_uefi_recovery_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);

	/* make sure that UEFI plugin is ready to receive devices */
	fu_plugin_add_rule(plugin, FU_PLUGIN_RULE_RUN_AFTER, "uefi_capsule");
	fu_plugin_add_flag(plugin, FWUPD_PLUGIN_FLAG_REQUIRE_HWID);
}

static void
fu_uefi_recovery_plugin_init(FuUefiRecoveryPlugin *self)
{
}

static void
fu_uefi_recovery_plugin_class_init(FuUefiRecoveryPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_uefi_recovery_plugin_constructed;
	plugin_class->coldplug = fu_uefi_recovery_plugin_coldplug;
	plugin_class->startup = fu_uefi_recovery_plugin_startup;
}
