/*
 * Copyright 2026 Sunwinon Electronics Co., Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-sunwinon-hid-device.h"
#include "fu-sunwinon-hid-plugin.h"

struct _FuSunwinonHidPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuSunwinonHidPlugin, fu_sunwinon_hid_plugin, FU_TYPE_PLUGIN)

static void
fu_sunwinon_hid_plugin_init(FuSunwinonHidPlugin *self)
{
}

static void
fu_sunwinon_hid_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_udev_subsystem(plugin, "hidraw");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_SUNWINON_HID_DEVICE);
}

static void
fu_sunwinon_hid_plugin_class_init(FuSunwinonHidPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_sunwinon_hid_plugin_constructed;
}
