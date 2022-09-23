/*
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-synaprom-device.h"
#include "fu-synaprom-firmware.h"
#include "fu-synaprom-plugin.h"

struct _FuSynapromPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuSynapromPlugin, fu_synaprom_plugin, FU_TYPE_PLUGIN)

static void
fu_synaprom_plugin_init(FuSynapromPlugin *self)
{
}

static void
fu_synaprom_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_set_name(plugin, "synaptics_prometheus");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_SYNAPROM_DEVICE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_SYNAPROM_FIRMWARE);
}

static void
fu_synaprom_plugin_class_init(FuSynapromPluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->constructed = fu_synaprom_plugin_constructed;
}
