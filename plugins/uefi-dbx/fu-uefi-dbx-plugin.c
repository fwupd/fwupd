/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-uefi-dbx-device.h"
#include "fu-uefi-dbx-plugin.h"

struct _FuUefiDbxPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuUefiDbxPlugin, fu_uefi_dbx_plugin, FU_TYPE_PLUGIN)

static gboolean
fu_uefi_dbx_plugin_coldplug(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	g_autoptr(FuUefiDbxDevice) device = fu_uefi_dbx_device_new(ctx);

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 99, "probe");
	fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 1, "setup");

	if (!fu_device_probe(FU_DEVICE(device), error))
		return FALSE;
	fu_progress_step_done(progress);

	if (!fu_device_setup(FU_DEVICE(device), error))
		return FALSE;
	fu_progress_step_done(progress);

	if (fu_context_has_hwid_flag(fu_plugin_get_context(plugin), "no-dbx-updates")) {
		fu_device_inhibit(FU_DEVICE(device),
				  "no-dbx",
				  "System firmware cannot accept DBX updates");
	}
	fu_plugin_device_add(plugin, FU_DEVICE(device));
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
	fu_plugin_add_rule(plugin, FU_PLUGIN_RULE_METADATA_SOURCE, "uefi_capsule");
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_EFI_SIGNATURE_LIST);
}

static void
fu_uefi_dbx_plugin_class_init(FuUefiDbxPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->constructed = fu_uefi_dbx_plugin_constructed;
	plugin_class->coldplug = fu_uefi_dbx_plugin_coldplug;
}
