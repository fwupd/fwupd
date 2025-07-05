/*
 * Copyright 2024 Chris Hofstaedtler <ch@zeha.at>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-rp-pico-device.h"
#include "fu-rp-pico-plugin.h"

struct _FuRpPicoPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuRpPicoPlugin, fu_rp_pico_plugin, FU_TYPE_PLUGIN)

static void
fu_rp_pico_plugin_init(FuRpPicoPlugin *self)
{
	fu_plugin_add_flag(FU_PLUGIN(self), FWUPD_PLUGIN_FLAG_MUTABLE_ENUMERATION);
}

static void
fu_rp_pico_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_RP_PICO_DEVICE);
}

static void
fu_rp_pico_plugin_class_init(FuRpPicoPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_rp_pico_plugin_constructed;
}
