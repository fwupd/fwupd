/*
 * Copyright 2026 SHENZHEN Betterlife Electronic CO.LTD <xiaomaoting@blestech.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-blestech-tp-firmware.h"
#include "fu-blestech-tp-hid-device.h"
#include "fu-blestech-tp-plugin.h"

struct _FuBlestechTpPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuBlestechTpPlugin, fu_blestech_tp_plugin, FU_TYPE_PLUGIN)

static void
fu_blestech_tp_plugin_init(FuBlestechTpPlugin *self)
{
}

static void
fu_blestech_tp_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_udev_subsystem(plugin, "hidraw");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_BLESTECH_TP_HID_DEVICE);
	fu_plugin_add_firmware_gtype(plugin, FU_TYPE_BLESTECH_TP_FIRMWARE);
}

static void
fu_blestech_tp_plugin_class_init(FuBlestechTpPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_blestech_tp_plugin_constructed;
}
