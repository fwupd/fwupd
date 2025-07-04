/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-uefi-db-device.h"
#include "fu-uefi-db-plugin.h"

struct _FuUefiDbPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuUefiDbPlugin, fu_uefi_db_plugin, FU_TYPE_PLUGIN)

static void
fu_uefi_db_plugin_init(FuUefiDbPlugin *self)
{
}

static void
fu_uefi_db_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_rule(plugin, FU_PLUGIN_RULE_METADATA_SOURCE, "uefi_pk");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_UEFI_DB_DEVICE);
}

static void
fu_uefi_db_plugin_class_init(FuUefiDbPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_uefi_db_plugin_constructed;
}
