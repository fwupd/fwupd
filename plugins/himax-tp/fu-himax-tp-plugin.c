/*
 * Copyright 2026 Himax Company, Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-himax-tp-firmware.h"
#include "fu-himax-tp-hid-device.h"
#include "fu-himax-tp-plugin.h"

struct _FuHimaxTpPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuHimaxTpPlugin, fu_himax_tp_plugin, FU_TYPE_PLUGIN)

static void
fu_himax_tp_plugin_init(FuHimaxTpPlugin *self)
{
}

static void
fu_himax_tp_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_udev_subsystem(plugin, "hidraw");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_HIMAX_TP_HID_DEVICE);
	fu_plugin_add_firmware_gtype(plugin, FU_TYPE_HIMAX_TP_FIRMWARE);
}

static void
fu_himax_tp_plugin_class_init(FuHimaxTpPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_himax_tp_plugin_constructed;
}
