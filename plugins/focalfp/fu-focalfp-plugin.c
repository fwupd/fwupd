/*
 * Copyright (C) 2022 Shihwei Huang <shihwei.huang@focaltech-electronics.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-focalfp-firmware.h"
#include "fu-focalfp-hid-device.h"
#include "fu-focalfp-plugin.h"

struct _FuFocalfpPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuFocalfpPlugin, fu_focalfp_plugin, FU_TYPE_PLUGIN)

static void
fu_focalfp_plugin_init(FuFocalfpPlugin *self)
{
}

static void
fu_focalfp_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_udev_subsystem(plugin, "hidraw");
	fu_plugin_add_firmware_gtype(plugin, NULL, FU_TYPE_FOCALFP_FIRMWARE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_FOCALFP_HID_DEVICE);
}

static void
fu_focalfp_plugin_class_init(FuFocalfpPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_focalfp_plugin_constructed;
}
