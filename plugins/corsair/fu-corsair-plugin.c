/*
 * Copyright (C) 2022 Andrii Dushko <andrii.dushko@developex.net>
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-corsair-device.h"
#include "fu-corsair-plugin.h"

struct _FuCorsairPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuCorsairPlugin, fu_corsair_plugin, FU_TYPE_PLUGIN)

static void
fu_corsair_plugin_init(FuCorsairPlugin *self)
{
}

static void
fu_corsair_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	FuContext *ctx = fu_plugin_get_context(plugin);
	fu_context_add_quirk_key(ctx, "CorsairDeviceKind");
	fu_context_add_quirk_key(ctx, "CorsairVendorInterfaceId");
	fu_context_add_quirk_key(ctx, "CorsairSubdeviceId");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_CORSAIR_DEVICE);
}

static void
fu_corsair_plugin_class_init(FuCorsairPluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->constructed = fu_corsair_plugin_constructed;
}
