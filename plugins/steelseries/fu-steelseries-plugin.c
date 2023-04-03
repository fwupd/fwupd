/*
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2022 Gaël PORTAY <gael.portay@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-steelseries-fizz-hid.h"
#include "fu-steelseries-fizz-tunnel.h"
#include "fu-steelseries-fizz.h"
#include "fu-steelseries-gamepad.h"
#include "fu-steelseries-mouse.h"
#include "fu-steelseries-plugin.h"
#include "fu-steelseries-sonic.h"

struct _FuSteelseriesPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuSteelseriesPlugin, fu_steelseries_plugin, FU_TYPE_PLUGIN)

static void
fu_steelseries_plugin_init(FuSteelseriesPlugin *self)
{
}

static void
fu_steelseries_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_STEELSERIES_FIZZ);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_STEELSERIES_FIZZ_HID);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_STEELSERIES_FIZZ_TUNNEL);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_STEELSERIES_GAMEPAD);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_STEELSERIES_MOUSE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_STEELSERIES_SONIC);
	fu_plugin_add_udev_subsystem(plugin, "hidraw");
}

static void
fu_steelseries_plugin_class_init(FuSteelseriesPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_steelseries_plugin_constructed;
}
