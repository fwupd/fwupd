/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-xy-kbms-config.h"
#include "fu-xy-kbms-device.h"
#include "fu-xy-kbms-firmware.h"
#include "fu-xy-kbms-pinmap.h"
#include "fu-xy-kbms-plugin.h"

struct _FuXyKbmsPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuXyKbmsPlugin, fu_xy_kbms_plugin, FU_TYPE_PLUGIN)

static void
fu_xy_kbms_plugin_init(FuXyKbmsPlugin *self)
{
	fu_plugin_add_flag(FU_PLUGIN(self), FWUPD_PLUGIN_FLAG_MUTABLE_ENUMERATION);
}

static void
fu_xy_kbms_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_set_device_gtype_default(plugin, FU_TYPE_XY_KBMS_DEVICE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_XY_KBMS_CONFIG); /* coverage */
	fu_plugin_add_firmware_gtype(plugin, FU_TYPE_XY_KBMS_FIRMWARE);
	fu_plugin_add_firmware_gtype(plugin, FU_TYPE_XY_KBMS_PINMAP); /* coverage */
	fu_plugin_add_udev_subsystem(plugin, "hidraw");

	/* chain up to parent */
	G_OBJECT_CLASS(fu_xy_kbms_plugin_parent_class)->constructed(obj);
}

static void
fu_xy_kbms_plugin_class_init(FuXyKbmsPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_xy_kbms_plugin_constructed;
}
