/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-uefi-dbx-device.h"
#include "fu-uefi-dbx-plugin.h"

struct _FuUefiDbxPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuUefiDbxPlugin, fu_uefi_dbx_plugin, FU_TYPE_PLUGIN)

static gboolean
fu_uefi_dbx_plugin_device_created(FuPlugin *plugin, FuDevice *device, GError **error)
{
	FuContext *ctx = fu_plugin_get_context(plugin);

	if (fu_context_has_hwid_flag(ctx, "no-dbx-updates")) {
		fu_device_inhibit(FU_DEVICE(device),
				  "no-dbx",
				  "System firmware cannot accept DBX updates");
	}

	/* success */
	return TRUE;
}

static void
fu_uefi_dbx_plugin_init(FuUefiDbxPlugin *self)
{
}

static void
fu_uefi_dbx_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	g_autoptr(FuVolume) esp = NULL;
	g_autoptr(GError) error_udisks2 = NULL;
	FuContext *ctx = fu_plugin_get_context(plugin);

	fu_plugin_add_rule(plugin, FU_PLUGIN_RULE_METADATA_SOURCE, "uefi_capsule");
	fu_plugin_add_rule(plugin, FU_PLUGIN_RULE_METADATA_SOURCE, "uefi_pk");
	fu_plugin_add_firmware_gtype(plugin, FU_TYPE_EFI_SIGNATURE_LIST);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_UEFI_DBX_DEVICE);

	/* ensure that an ESP was found */
	esp = fu_context_get_default_esp(ctx, &error_udisks2);
	if (esp == NULL) {
		g_info("cannot find default ESP: %s", error_udisks2->message);
		fu_plugin_add_flag(plugin, FWUPD_PLUGIN_FLAG_ESP_NOT_FOUND);
		fu_plugin_add_flag(plugin, FWUPD_PLUGIN_FLAG_CLEAR_UPDATABLE);
		fu_plugin_add_flag(plugin, FWUPD_PLUGIN_FLAG_USER_WARNING);
	}
}

static void
fu_uefi_dbx_plugin_class_init(FuUefiDbxPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_uefi_dbx_plugin_constructed;
	plugin_class->device_created = fu_uefi_dbx_plugin_device_created;
}
