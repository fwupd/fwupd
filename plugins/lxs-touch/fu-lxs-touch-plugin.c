/*
 * Copyright 2026 JS Park <mameforever2@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-lxs-touch-device.h"
#include "fu-lxs-touch-firmware.h"
#include "fu-lxs-touch-plugin.h"

struct _FuLxsTouchPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuLxsTouchPlugin, fu_lxs_touch_plugin, FU_TYPE_PLUGIN)

static void
fu_lxs_touch_plugin_init(FuLxsTouchPlugin *self)
{
}

static void
fu_lxs_touch_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_udev_subsystem(plugin, "hidraw");
	fu_plugin_add_firmware_gtype(plugin, FU_TYPE_LXS_TOUCH_FIRMWARE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_LXS_TOUCH_DEVICE);

	/* chain up to parent */
	G_OBJECT_CLASS(fu_lxs_touch_plugin_parent_class)->constructed(obj);
}

static void
fu_lxs_touch_plugin_class_init(FuLxsTouchPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_lxs_touch_plugin_constructed;
}
