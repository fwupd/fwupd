/*
 * Copyright 2024 Algoltek <Algoltek, Inc.>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-algoltek-aux-device.h"
#include "fu-algoltek-aux-firmware.h"
#include "fu-algoltek-aux-plugin.h"

struct _FuAlgoltekAuxPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuAlgoltekAuxPlugin, fu_algoltek_aux_plugin, FU_TYPE_PLUGIN)

static void
fu_algoltek_aux_plugin_init(FuAlgoltekAuxPlugin *self)
{
}

static void
fu_algoltek_aux_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_udev_subsystem(plugin, "drm"); /* used for uevent only */
	fu_plugin_add_udev_subsystem(plugin, "drm_dp_aux_dev");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_ALGOLTEK_AUX_DEVICE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_ALGOLTEK_AUX_FIRMWARE);
}

static void
fu_algoltek_aux_plugin_class_init(FuAlgoltekAuxPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_algoltek_aux_plugin_constructed;
}
