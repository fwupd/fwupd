/*
 * Copyright (C) 2023 GN Audio A/S
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-jabra-gnp-device.h"
#include "fu-jabra-gnp-firmware.h"
#include "fu-jabra-gnp-plugin.h"

struct _FuJabraGnpPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuJabraGnpPlugin, fu_jabra_gnp_plugin, FU_TYPE_PLUGIN)

static void
fu_jabra_gnp_plugin_init(FuJabraGnpPlugin *self)
{
}

static void
fu_jabra_gnp_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	FuContext *ctx = fu_plugin_get_context(plugin);
	fu_context_add_quirk_key(ctx, "JabraGnpAddress");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_JABRA_GNP_DEVICE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_JABRA_GNP_FIRMWARE);
}

static void
fu_jabra_gnp_plugin_class_init(FuJabraGnpPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_jabra_gnp_plugin_constructed;
}
