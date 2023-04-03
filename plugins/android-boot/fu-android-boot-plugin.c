/*
 * Copyright (C) 2022 Dylan Van Assche <me@dylanvanassche.be>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-android-boot-device.h"
#include "fu-android-boot-plugin.h"

struct _FuAndroidBootPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuAndroidBootPlugin, fu_android_boot_plugin, FU_TYPE_PLUGIN)

static void
fu_android_boot_plugin_init(FuAndroidBootPlugin *self)
{
}

static void
fu_android_boot_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_ANDROID_BOOT_DEVICE);
	fu_plugin_add_udev_subsystem(plugin, "block");
}

static void
fu_android_boot_plugin_class_init(FuAndroidBootPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_android_boot_plugin_constructed;
}
