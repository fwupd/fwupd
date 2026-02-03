/*
 * Copyright 2026 SHENZHEN Betterlife Electronic CO.LTD <xiaomaoting@blestech.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-blestechtp-common.h"
#include "fu-blestechtp-plugin.h"
#include "fu-blestechtp-firmware.h"
#include "fu-blestechtp-hid-device.h"

struct _FuBlestechtpPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuBlestechtpPlugin, fu_blestechtp_plugin, FU_TYPE_PLUGIN)

static void
fu_blestechtp_plugin_init(FuBlestechtpPlugin *self)
{
}

static void
fu_blestechtp_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	
	fu_plugin_add_udev_subsystem(plugin, "hidraw");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_BLESTECHTP_HID_DEVICE); 
	fu_plugin_add_firmware_gtype(plugin, FU_TYPE_BLESTECHTP_FIRMWARE);
}

static void
fu_blestechtp_plugin_class_init(FuBlestechtpPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_blestechtp_plugin_constructed;
}
