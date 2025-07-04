/*
 * Copyright 2023 Dell Technologies
 * Copyright 2023 Mediatek Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-mediatek-scaler-device.h"
#include "fu-mediatek-scaler-firmware.h"
#include "fu-mediatek-scaler-plugin.h"

struct _FuMediatekScalerPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuMediatekScalerPlugin, fu_mediatek_scaler_plugin, FU_TYPE_PLUGIN)

static void
fu_mediatek_scaler_plugin_init(FuMediatekScalerPlugin *self)
{
}

static void
fu_mediatek_scaler_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_udev_subsystem(plugin, "drm");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_MEDIATEK_SCALER_DEVICE);
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_MEDIATEK_SCALER_FIRMWARE);
}

static void
fu_mediatek_scaler_plugin_class_init(FuMediatekScalerPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_mediatek_scaler_plugin_constructed;
}
