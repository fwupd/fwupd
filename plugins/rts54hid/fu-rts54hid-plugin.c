/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-rts54hid-device.h"
#include "fu-rts54hid-module.h"
#include "fu-rts54hid-plugin.h"

struct _FuRts54HidPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuRts54HidPlugin, fu_rts54hid_plugin, FU_TYPE_PLUGIN)

static void
fu_rts54hid_plugin_init(FuRts54HidPlugin *self)
{
}

static void
fu_rts54hid_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	FuContext *ctx = fu_plugin_get_context(plugin);
	fu_context_add_quirk_key(ctx, "Rts54TargetAddr");
	fu_context_add_quirk_key(ctx, "Rts54I2cSpeed");
	fu_context_add_quirk_key(ctx, "Rts54RegisterAddrLen");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_RTS54HID_DEVICE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_RTS54HID_MODULE);
}

static void
fu_rts54hid_plugin_class_init(FuRts54HidPluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->constructed = fu_rts54hid_plugin_constructed;
}
