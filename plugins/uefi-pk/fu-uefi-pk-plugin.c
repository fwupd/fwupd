/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-uefi-pk-device.h"
#include "fu-uefi-pk-plugin.h"

struct _FuUefiPkPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuUefiPkPlugin, fu_uefi_pk_plugin, FU_TYPE_PLUGIN)

static gboolean
fu_uefi_pk_plugin_coldplug(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuContext *ctx = fu_plugin_get_context(plugin);
	g_autoptr(FuUefiPkDevice) device = fu_uefi_pk_device_new(ctx);
	if (!fu_device_setup(FU_DEVICE(device), error))
		return FALSE;
	fu_plugin_device_add(plugin, FU_DEVICE(device));
	return TRUE;
}

static void
fu_uefi_pk_plugin_init(FuUefiPkPlugin *self)
{
}

static void
fu_uefi_pk_plugin_class_init(FuUefiPkPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->coldplug = fu_uefi_pk_plugin_coldplug;
}
