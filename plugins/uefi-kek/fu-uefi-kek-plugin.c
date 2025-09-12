/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-uefi-kek-device.h"
#include "fu-uefi-kek-plugin.h"

struct _FuUefiKekPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuUefiKekPlugin, fu_uefi_kek_plugin, FU_TYPE_PLUGIN)

static gboolean
fu_uefi_kek_plugin_device_created(FuPlugin *plugin, FuDevice *device, GError **error)
{
	/* check for HWID-based quirk to disable KEK updates */
	if (fu_context_has_hwid_flag(fu_plugin_get_context(plugin), "no-kek-updates")) {
		fu_device_inhibit(FU_DEVICE(device),
				  "no-kek",
				  "system has invalid test platform key");
	}

	/* success */
	return TRUE;
}

static void
fu_uefi_kek_plugin_init(FuUefiKekPlugin *self)
{
}

static void
fu_uefi_kek_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_rule(plugin, FU_PLUGIN_RULE_METADATA_SOURCE, "uefi_pk");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_UEFI_KEK_DEVICE);
}

static void
fu_uefi_kek_plugin_class_init(FuUefiKekPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_uefi_kek_plugin_constructed;
	plugin_class->device_created = fu_uefi_kek_plugin_device_created;
}
