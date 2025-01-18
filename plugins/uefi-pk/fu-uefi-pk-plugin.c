/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-uefi-pk-device.h"
#include "fu-uefi-pk-plugin.h"

struct _FuUefiPkPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuUefiPkPlugin, fu_uefi_pk_plugin, FU_TYPE_PLUGIN)

static void
fu_uefi_pk_plugin_init(FuUefiPkPlugin *self)
{
}

static void
fu_uefi_pk_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_set_device_gtype_default(plugin, FU_TYPE_UEFI_PK_DEVICE);
}

static void
fu_uefi_pk_plugin_class_init(FuUefiPkPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_uefi_pk_plugin_constructed;
}
