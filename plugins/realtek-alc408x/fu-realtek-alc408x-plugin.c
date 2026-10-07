/*
 * Copyright 2026 NVIDIA Corporation
 * Author: Vishnu Raghav <vraghav@nvidia.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-realtek-alc408x-device.h"
#include "fu-realtek-alc408x-firmware.h"
#include "fu-realtek-alc408x-plugin.h"

struct _FuRealtekAlc408xPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuRealtekAlc408xPlugin, fu_realtek_alc408x_plugin, FU_TYPE_PLUGIN)

static void
fu_realtek_alc408x_plugin_init(FuRealtekAlc408xPlugin *self)
{
}

static void
fu_realtek_alc408x_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_udev_subsystem(plugin, "usb");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_REALTEK_ALC408X_DEVICE);
	fu_plugin_add_firmware_gtype(plugin, FU_TYPE_REALTEK_ALC408X_FIRMWARE);

	/* chain up to parent */
	G_OBJECT_CLASS(fu_realtek_alc408x_plugin_parent_class)->constructed(obj);
}

static void
fu_realtek_alc408x_plugin_class_init(FuRealtekAlc408xPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_realtek_alc408x_plugin_constructed;
}
