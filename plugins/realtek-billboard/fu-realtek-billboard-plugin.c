/*
 * Copyright 2026 Realtek Corporation
 * Copyright 2026 Shadow Zhang <shadow_zhang@realsil.com.cn>
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "config.h"

#include "fu-realtek-billboard-device.h"
#include "fu-realtek-billboard-plugin.h"

struct _FuRealtekBillboardPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuRealtekBillboardPlugin, fu_realtek_billboard_plugin, FU_TYPE_PLUGIN)

static void
fu_realtek_billboard_plugin_init(FuRealtekBillboardPlugin *self)
{
}

static void
fu_realtek_billboard_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_udev_subsystem(plugin, "usb");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_REALTEK_BILLBOARD_DEVICE);

	/* chain up to parent */
	G_OBJECT_CLASS(fu_realtek_billboard_plugin_parent_class)->constructed(obj);
}

static void
fu_realtek_billboard_plugin_class_init(FuRealtekBillboardPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_realtek_billboard_plugin_constructed;
}
