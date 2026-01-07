/*
 * Copyright 2025 Raydium.inc <Maker.Tsai@rad-ic.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-raydiumtp-firmware.h"
#include "fu-raydiumtp-hid-device.h"
#include "fu-raydiumtp-plugin.h"

struct _FuRaydiumtpPlugin
{
    FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuRaydiumtpPlugin, fu_raydiumtp_plugin, FU_TYPE_PLUGIN)

static void
fu_raydiumtp_plugin_init(FuRaydiumtpPlugin *self)
{
}

static void
fu_raydiumtp_plugin_constructed(GObject *obj)
{
    FuPlugin *plugin = FU_PLUGIN(obj);
    fu_plugin_add_udev_subsystem(plugin, "hidraw");
    fu_plugin_set_device_gtype_default(plugin, FU_TYPE_RAYDIUMTP_HID_DEVICE);
    fu_plugin_add_device_gtype(plugin, FU_TYPE_RAYDIUMTP_HID_DEVICE);
    fu_plugin_add_firmware_gtype(plugin, FU_TYPE_RAYDIUMTP_FIRMWARE);
}

static void
fu_raydiumtp_plugin_class_init(FuRaydiumtpPluginClass *klass)
{
    FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
    plugin_class->constructed = fu_raydiumtp_plugin_constructed;
}
