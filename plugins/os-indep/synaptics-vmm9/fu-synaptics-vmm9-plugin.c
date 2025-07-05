/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-synaptics-vmm9-device.h"
#include "fu-synaptics-vmm9-firmware.h"
#include "fu-synaptics-vmm9-plugin.h"

struct _FuSynapticsVmm9Plugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuSynapticsVmm9Plugin, fu_synaptics_vmm9_plugin, FU_TYPE_PLUGIN)

static void
fu_synaptics_vmm9_plugin_init(FuSynapticsVmm9Plugin *self)
{
	fu_plugin_add_flag(FU_PLUGIN(self), FWUPD_PLUGIN_FLAG_MUTABLE_ENUMERATION);
}

static void
fu_synaptics_vmm9_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_SYNAPTICS_VMM9_DEVICE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_SYNAPTICS_VMM9_FIRMWARE);
}

static void
fu_synaptics_vmm9_plugin_class_init(FuSynapticsVmm9PluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_synaptics_vmm9_plugin_constructed;
}
