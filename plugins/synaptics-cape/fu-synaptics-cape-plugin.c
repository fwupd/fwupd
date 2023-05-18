/*
 * Copyright (C) 2021 Synaptics Incorporated <simon.ho@synaptics.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-synaptics-cape-device.h"
#include "fu-synaptics-cape-hid-firmware.h"
#include "fu-synaptics-cape-plugin.h"

struct _FuSynapticsCapePlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuSynapticsCapePlugin, fu_synaptics_cape_plugin, FU_TYPE_PLUGIN)

static void
fu_synaptics_cape_plugin_init(FuSynapticsCapePlugin *self)
{
}

static void
fu_synaptics_cape_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_SYNAPTICS_CAPE_DEVICE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_SYNAPTICS_CAPE_HID_FIRMWARE);
}

static void
fu_synaptics_cape_plugin_class_init(FuSynapticsCapePluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_synaptics_cape_plugin_constructed;
}
