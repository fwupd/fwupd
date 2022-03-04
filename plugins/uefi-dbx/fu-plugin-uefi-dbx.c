/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-uefi-dbx-device.h"

static void
fu_plugin_uefi_dbx_init(FuPlugin *plugin)
{
	fu_plugin_add_rule(plugin, FU_PLUGIN_RULE_METADATA_SOURCE, "uefi_capsule");
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_EFI_SIGNATURE_LIST);
}

static gboolean
fu_plugin_uefi_dbx_coldplug(FuPlugin *plugin, GError **error)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	g_autoptr(FuUefiDbxDevice) device = fu_uefi_dbx_device_new(ctx);
	if (!fu_device_probe(FU_DEVICE(device), error))
		return FALSE;
	if (!fu_device_setup(FU_DEVICE(device), error))
		return FALSE;
	if (fu_context_has_hwid_flag(fu_plugin_get_context(plugin), "no-dbx-updates")) {
		fu_device_inhibit(FU_DEVICE(device),
				  "no-dbx",
				  "System firmware cannot accept DBX updates");
	}
	fu_plugin_device_add(plugin, FU_DEVICE(device));
	return TRUE;
}

void
fu_plugin_init_vfuncs(FuPluginVfuncs *vfuncs)
{
	vfuncs->build_hash = FU_BUILD_HASH;
	vfuncs->init = fu_plugin_uefi_dbx_init;
	vfuncs->coldplug = fu_plugin_uefi_dbx_coldplug;
}
