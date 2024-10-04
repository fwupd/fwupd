/*
 * Copyright 2024 Randy Lai <randy.lai@weidahitech.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-weida-raw-device.h"
#include "fu-weida-raw-firmware.h"
#include "fu-weida-raw-plugin.h"

struct _FuWeidaRawPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuWeidaRawPlugin, fu_weida_raw_plugin, FU_TYPE_PLUGIN)

static void
fu_weida_raw_plugin_init(FuWeidaRawPlugin *self)
{
}

static void
fu_weida_raw_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_WEIDA_RAW_DEVICE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_WEIDA_RAW_FIRMWARE);
}

static void
fu_weida_raw_plugin_class_init(FuWeidaRawPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_weida_raw_plugin_constructed;
}
