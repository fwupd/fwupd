/*
 * Copyright (C) 2022 Intel
 *
 * SPDX-License-Identifier: LGPL-2.1+ OR Apache-2.0
 */

#include "config.h"

#include "fu-igsc-aux-firmware.h"
#include "fu-igsc-code-firmware.h"
#include "fu-igsc-device.h"
#include "fu-igsc-oprom-firmware.h"
#include "fu-igsc-plugin.h"

struct _FuIgscPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuIgscPlugin, fu_igsc_plugin, FU_TYPE_PLUGIN)

static void
fu_igsc_plugin_init(FuIgscPlugin *self)
{
}

static void
fu_igsc_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_udev_subsystem(plugin, "mei");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_IGSC_DEVICE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_IGSC_CODE_FIRMWARE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_IGSC_AUX_FIRMWARE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_IGSC_OPROM_FIRMWARE);
}

static void
fu_igsc_plugin_class_init(FuIgscPluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->constructed = fu_igsc_constructed;
}
