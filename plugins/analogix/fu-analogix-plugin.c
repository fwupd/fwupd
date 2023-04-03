/*
 * Copyright (C) 2021 Xiaotian Cui <xtcui@analogixsemi.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-analogix-device.h"
#include "fu-analogix-firmware.h"
#include "fu-analogix-plugin.h"

struct _FuAnalogixPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuAnalogixPlugin, fu_analogix_plugin, FU_TYPE_PLUGIN)

static void
fu_analogix_plugin_init(FuAnalogixPlugin *self)
{
}

static void
fu_analogix_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_ANALOGIX_DEVICE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_ANALOGIX_FIRMWARE);
}

static void
fu_analogix_plugin_class_init(FuAnalogixPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_analogix_plugin_constructed;
}
