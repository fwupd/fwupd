/*
 * Copyright 2026 Sean Rhodes <sean@starlabs.systems>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-sunplus-camera-device.h"
#include "fu-sunplus-camera-plugin.h"

struct _FuSunplusCameraPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuSunplusCameraPlugin, fu_sunplus_camera_plugin, FU_TYPE_PLUGIN)

static void
fu_sunplus_camera_plugin_init(FuSunplusCameraPlugin *self)
{
}

static void
fu_sunplus_camera_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_udev_subsystem(plugin, "video4linux");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_SUNPLUS_CAMERA_DEVICE);
	G_OBJECT_CLASS(fu_sunplus_camera_plugin_parent_class)->constructed(obj);
}

static void
fu_sunplus_camera_plugin_class_init(FuSunplusCameraPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_sunplus_camera_plugin_constructed;
}
