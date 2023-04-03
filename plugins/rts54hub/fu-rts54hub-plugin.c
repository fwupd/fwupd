/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-rts54hub-device.h"
#include "fu-rts54hub-plugin.h"
#include "fu-rts54hub-rtd21xx-background.h"
#include "fu-rts54hub-rtd21xx-foreground.h"

struct _FuRts54HubPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuRts54HubPlugin, fu_rts54hub_plugin, FU_TYPE_PLUGIN)

static void
fu_rts54hub_plugin_init(FuRts54HubPlugin *self)
{
}

static void
fu_rts54hub_plugin_object_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_set_name(plugin, "rts54hub");
}

static void
fu_rts54hub_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	FuContext *ctx = fu_plugin_get_context(plugin);
	fu_context_add_quirk_key(ctx, "Rts54TargetAddr");
	fu_context_add_quirk_key(ctx, "Rts54I2cSpeed");
	fu_context_add_quirk_key(ctx, "Rts54RegisterAddrLen");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_RTS54HUB_DEVICE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_RTS54HUB_RTD21XX_BACKGROUND);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_RTS54HUB_RTD21XX_FOREGROUND);
}

static void
fu_rts54hub_plugin_class_init(FuRts54HubPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->constructed = fu_rts54hub_plugin_object_constructed;
	plugin_class->constructed = fu_rts54hub_plugin_constructed;
}
