/*
 * Copyright (C) 2021 Peter Marheine <pmarheine@chromium.org>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-realtek-mst-device.h"
#include "fu-realtek-mst-plugin.h"

struct _FuRealtekMstPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuRealtekMstPlugin, fu_realtek_mst_plugin, FU_TYPE_PLUGIN)

static void
fu_realtek_mst_plugin_init(FuRealtekMstPlugin *self)
{
}

static void
fu_realtek_mst_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	FuContext *ctx = fu_plugin_get_context(plugin);
	fu_context_add_quirk_key(ctx, "RealtekMstDpAuxName");
	fu_context_add_quirk_key(ctx, "RealtekMstDrmCardKernelName");
	fu_plugin_add_udev_subsystem(plugin, "i2c");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_REALTEK_MST_DEVICE);
}

static void
fu_realtek_mst_plugin_class_init(FuRealtekMstPluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->constructed = fu_realtek_mst_plugin_constructed;
}
