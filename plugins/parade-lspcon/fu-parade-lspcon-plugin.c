/*
 * Copyright (C) 2021 Peter Marheine <pmarheine@chromium.org>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-parade-lspcon-device.h"
#include "fu-parade-lspcon-plugin.h"

struct _FuParadeLspconPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuParadeLspconPlugin, fu_parade_lspcon_plugin, FU_TYPE_PLUGIN)

static void
fu_parade_lspcon_plugin_init(FuParadeLspconPlugin *self)
{
}

static void
fu_parade_lspcon_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	FuContext *ctx = fu_plugin_get_context(plugin);
	fu_context_add_quirk_key(ctx, "ParadeLspconAuxDeviceName");
	fu_plugin_add_udev_subsystem(plugin, "i2c");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_PARADE_LSPCON_DEVICE);
}

static void
fu_parade_lspcon_plugin_class_init(FuParadeLspconPluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->constructed = fu_parade_lspcon_plugin_constructed;
}
