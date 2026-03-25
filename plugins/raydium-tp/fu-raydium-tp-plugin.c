/*
 * Copyright 2025 Raydium.inc <Maker.Tsai@rad-ic.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-raydium-tp-firmware.h"
#include "fu-raydium-tp-hid-device.h"
#include "fu-raydium-tp-plugin.h"

struct _FuRaydiumTpPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuRaydiumTpPlugin, fu_raydium_tp_plugin, FU_TYPE_PLUGIN)

static void
fu_raydium_tp_plugin_init(FuRaydiumTpPlugin *self)
{
}

static void
fu_raydium_tp_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_udev_subsystem(plugin, "hidraw");
	fu_plugin_set_device_gtype_default(plugin, FU_TYPE_RAYDIUM_TP_HID_DEVICE);
	fu_plugin_add_firmware_gtype(plugin, FU_TYPE_RAYDIUM_TP_FIRMWARE);

	/* chain up to parent */
	G_OBJECT_CLASS(fu_raydium_tp_plugin_parent_class)->constructed(obj);
}

static void
fu_raydium_tp_plugin_class_init(FuRaydiumTpPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_raydium_tp_plugin_constructed;
}
