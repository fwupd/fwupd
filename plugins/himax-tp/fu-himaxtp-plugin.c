/*
 * Copyright 2026 Himax Company, Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-himaxtp-firmware.h"
#include "fu-himaxtp-hid-device.h"
#include "fu-himaxtp-plugin.h"

struct _FuHimaxtpPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuHimaxtpPlugin, fu_himaxtp_plugin, FU_TYPE_PLUGIN)

static void
fu_himaxtp_plugin_init(FuHimaxtpPlugin *self)
{
}

static void
fu_himaxtp_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_udev_subsystem(plugin, "hidraw");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_HIMAXTP_HID_DEVICE);
	fu_plugin_add_firmware_gtype(plugin, FU_TYPE_HIMAXTP_FIRMWARE);
}

static void
fu_himaxtp_plugin_class_init(FuHimaxtpPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_himaxtp_plugin_constructed;
}
