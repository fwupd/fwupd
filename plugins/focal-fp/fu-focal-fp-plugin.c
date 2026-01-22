/*
 * Copyright 2022 Shihwei Huang <shihwei.huang@focaltech-electronics.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-focal-fp-firmware.h"
#include "fu-focal-fp-hid-device.h"
#include "fu-focal-fp-plugin.h"

struct _FuFocalFpPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuFocalFpPlugin, fu_focal_fp_plugin, FU_TYPE_PLUGIN)

static void
fu_focal_fp_plugin_init(FuFocalFpPlugin *self)
{
}

static void
fu_focal_fp_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_udev_subsystem(plugin, "hidraw");
	fu_plugin_add_firmware_gtype(plugin, FU_TYPE_FOCAL_FP_FIRMWARE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_FOCAL_FP_HID_DEVICE);
}

static void
fu_focal_fp_plugin_class_init(FuFocalFpPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_focal_fp_plugin_constructed;
}
